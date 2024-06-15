// This implements a 16-bit 48kHz stereo TCP PCM receiver on an esp32-a1s audio kit
// https://docs.ai-thinker.com/en/esp32-audio-kit
// Written by Netham45

// Board Library: esp32 by Espressif Systems Version 3.0.1
// Board Library URL: https://espressif.github.io/arduino-esp32/package_esp32_dev_index.json
// Tools -> Board: ESP32 Dev Module

#include "esp_wifi.h"                    // ESP IDF
#include "esp_psram.h"                   // ESP IDF
#include "AudioTools.h"                  // Needs https://github.com/pschatzmann/arduino-audio-tools
#include "AudioLibs/AudioBoardStream.h"  // Needs https://github.com/pschatzmann/arduino-audio-driver
#include "wifi_secrets.h"                // In project

#define SAMPLE_RATE 48000          // Sample rate for incoming PCM, configurable
#define BIT_DEPTH 16               // Bit depth for incoming PCM, non-configurable (does not implement 24->32 bit padding)
#define CHANNELS 2                 // Channels for incoming PCM, non-configurable (Only implements stereo)
#define VOLUME 1.0f                // Volume 0.0f-1.0f, distorts at 100%, configurable
#define PORT 4010                  // TCP port for Scream server data, configurable
#define SERVER "192.168.3.114"     // Scream server IP
#define INITIAL_BUFFER_SIZE 8      // Number of chunks to be buffered before playback starts, configurable
#define SOUNDCHIP_PREFEED_SIZE 8   // Number of chunks to be sent to the sound chip once initial buffer is reached, configurable
#define BUFFER_GROW_STEP_SIZE 8    // Number of chunks to add each underflow, configurable
#define MAX_BUFFER_SIZE 3200       // Max number of chunks to be buffered before packets are dropped, configurable
#define CHUNK_SIZE 1152            // PCM Bytes per chunk, non-configurable (Part of Scream)
#define HEADER_SIZE 13             // Scream Header byte size, non-configurable (Part of Scream)
#define RAM_BUFFER_CHUNK_COUNT 64
  // Number of chunks to be buffered to RAM from psram
                                   // Samples per chunk
#define SAMPLES_PER_CHUNK (CHUNK_SIZE / CHANNELS / (BIT_DEPTH / 8))
#define I2S_AUDIO_TYPE AudioKitEs8388V1  // Type of audio board being used
//#define USE_RAM_BUFFER 1 // Use RAM buffer, there's a clicking noise every once in a while without this

#ifdef USE_RAM_BUFFER
#if (MAX_BUFFER_SIZE % RAM_BUFFER_CHUNK_COUNT != 0)
#error "MAX_BUFFER_SIZE must be a multiple of RAM_BUFFER_CHUNK_COUNT
#endif
#endif

WiFiClient client;                // PCM TCP handler
uint64_t received_packets = 0;    // Number of received packets since last buffer fill
uint64_t played_packets = 0;      // Number of packets sent to audio card
uint64_t packet_buffer_size = 0;  // Number of packets in ring buffer
uint64_t packet_buffer_pos = 0;   // Position of ring buffer read head
bool is_underrun = true;          // True if currently underrun
                                  // Buffer of packets to send
uint8_t *packet_buffer[MAX_BUFFER_SIZE] = { 0 };
// Number of bytes to buffer
uint64_t target_buffer_size = INITIAL_BUFFER_SIZE;
// Audio Out I2S Stream
uint64_t last_packet_id = 0;  // ID of last packet, temp
                              // TCP input buffer
uint8_t in_buffer[CHUNK_SIZE + HEADER_SIZE];
AudioBoardStream audio_out(I2S_AUDIO_TYPE);

uint8_t* ram_buffer_1[RAM_BUFFER_CHUNK_COUNT] = { 0 };
uint8_t* ram_buffer_2[RAM_BUFFER_CHUNK_COUNT] = { 0 };
uint8_t active_ram_buffer = 1;
uint8_t last_filled_ram_buffer = 0;
bool ram_buffer_1_empty = true;
bool ram_buffer_2_empty = true;
uint64_t ram_buffer_pos = 0;

portMUX_TYPE buffer_mutex = portMUX_INITIALIZER_UNLOCKED;

bool IRAM_ATTR push_chunk(uint8_t *chunk) {
  taskENTER_CRITICAL(&buffer_mutex);
  if (packet_buffer_size == MAX_BUFFER_SIZE) {
    taskEXIT_CRITICAL(&buffer_mutex);
    return false;
  }
  int write_position = (packet_buffer_pos + packet_buffer_size) % MAX_BUFFER_SIZE;
  memcpy(packet_buffer[write_position], chunk, CHUNK_SIZE);
  packet_buffer_size++;
  taskEXIT_CRITICAL(&buffer_mutex);
  return true;
}

uint8_t *IRAM_ATTR pop_chunk() {
  taskENTER_CRITICAL(&buffer_mutex);
  if (packet_buffer_size == 0){
#ifndef USE_RAM_BUFFER
    Serial.println("Tried to pop from buffer, but it's empty.");
    if (!is_underrun) {

      Serial.println("Buffer underflow");
      received_packets = 0;
      played_packets = 0;
      target_buffer_size += BUFFER_GROW_STEP_SIZE;
      if (target_buffer_size >= MAX_BUFFER_SIZE) {
        Serial.println("Buffer at MAX_BUFFER_SIZE");
        target_buffer_size = MAX_BUFFER_SIZE;
      }
    }
    is_underrun = true;
#endif
    taskEXIT_CRITICAL(&buffer_mutex);
    return NULL;
  }
  uint8_t *return_chunk = packet_buffer[packet_buffer_pos];
  packet_buffer_size--;
  packet_buffer_pos = (packet_buffer_pos + 1) % MAX_BUFFER_SIZE;
  taskEXIT_CRITICAL(&buffer_mutex);
  return return_chunk;
}

void IRAM_ATTR buffer_load() {
#ifndef USE_RAM_BUFFER
  return;
#endif
  taskENTER_CRITICAL(&buffer_mutex);
  if (ram_buffer_1_empty && packet_buffer_size >= RAM_BUFFER_CHUNK_COUNT && !last_filled_ram_buffer) {
    packet_buffer_pos = (packet_buffer_pos + RAM_BUFFER_CHUNK_COUNT) % MAX_BUFFER_SIZE;
    packet_buffer_size -= RAM_BUFFER_CHUNK_COUNT;
    memcpy(ram_buffer_1[0], packet_buffer[packet_buffer_pos], RAM_BUFFER_CHUNK_COUNT * CHUNK_SIZE);
    ram_buffer_1_empty = false;
    last_filled_ram_buffer = 1;
  }

  if (ram_buffer_2_empty && packet_buffer_size >= RAM_BUFFER_CHUNK_COUNT && last_filled_ram_buffer) {
    packet_buffer_pos = (packet_buffer_pos + RAM_BUFFER_CHUNK_COUNT) % MAX_BUFFER_SIZE;
    packet_buffer_size -= RAM_BUFFER_CHUNK_COUNT;
    memcpy(ram_buffer_2[0], packet_buffer[packet_buffer_pos], RAM_BUFFER_CHUNK_COUNT * CHUNK_SIZE);
    ram_buffer_2_empty = false;
    last_filled_ram_buffer = 0;
  }
  taskEXIT_CRITICAL(&buffer_mutex);
}

uint8_t IRAM_ATTR *pop_ram_chunk() {
#ifndef USE_RAM_BUFFER
  return pop_chunk();
#else
  taskENTER_CRITICAL(&buffer_mutex);
  if (ram_buffer_1_empty && ram_buffer_2_empty) {
    Serial.println("Tried to pop from ram buffer, but it's empty.");
    if (!is_underrun) {
      audio_out.writeSilence(CHUNK_SIZE * 25);
      Serial.println("Buffer underflow");
      received_packets = 0;
      played_packets = 0;
      target_buffer_size += BUFFER_GROW_STEP_SIZE;
      if (target_buffer_size >= MAX_BUFFER_SIZE) {
        Serial.println("Buffer at MAX_BUFFER_SIZE");
        target_buffer_size = MAX_BUFFER_SIZE;
      }
    }
    is_underrun = true;
    taskEXIT_CRITICAL(&buffer_mutex);
    return NULL;
  }

  uint8_t** ram_buffer = active_ram_buffer==1 ? ram_buffer_1 : ram_buffer_2;
  uint64_t orig_ram_buffer_pos = ram_buffer_pos;
  ram_buffer_pos = ram_buffer_pos + 1;
  if (ram_buffer_pos == RAM_BUFFER_CHUNK_COUNT) {
    if (active_ram_buffer == 1)
      ram_buffer_1_empty = true;
    else
      ram_buffer_2_empty = true;
    active_ram_buffer = (active_ram_buffer + 1) % 2;
    ram_buffer_pos = 0;
  }
  taskEXIT_CRITICAL(&buffer_mutex);
  return ram_buffer[orig_ram_buffer_pos];
#endif
}

void sleep() {
  audio_out.setActive(false);
  audio_out.end();
  esp_deep_sleep_start();
}

void setup_audio_buffers() {
  Serial.println("Allocating buffer");
#ifdef USE_RAM_BUFFER
  uint8_t* _ram_buffer_1 = 0;
  _ram_buffer_1 = (uint8_t*)malloc(CHUNK_SIZE * RAM_BUFFER_CHUNK_COUNT);
  memset(_ram_buffer_1, 0, CHUNK_SIZE * RAM_BUFFER_CHUNK_COUNT);
  for (int i = 0; i < RAM_BUFFER_CHUNK_COUNT; i++)
    ram_buffer_1[i] = (uint8_t*)(_ram_buffer_1 + i * CHUNK_SIZE);

  uint8_t* _ram_buffer_2 = 0;
  _ram_buffer_2 = (uint8_t*)malloc(CHUNK_SIZE * RAM_BUFFER_CHUNK_COUNT);
  memset(_ram_buffer_2, 0, CHUNK_SIZE * RAM_BUFFER_CHUNK_COUNT);
  for (int i = 0; i < RAM_BUFFER_CHUNK_COUNT; i++)
    ram_buffer_2[i] = (uint8_t*)(_ram_buffer_2 + i * CHUNK_SIZE);
#endif

  uint8_t* buffer = 0;
  buffer = (uint8_t*)ps_malloc(CHUNK_SIZE * MAX_BUFFER_SIZE);
  memset(buffer, 0, CHUNK_SIZE * MAX_BUFFER_SIZE);
  for (int i = 0; i < MAX_BUFFER_SIZE; i++)
    packet_buffer[i] = (uint8_t*)buffer + i * CHUNK_SIZE;
  Serial.println("Buffer allocated");
}

void setup_audio() {
  AudioLogger::instance().begin(Serial, AudioLogger::Info);
  I2SCodecConfig cfg = audio_out.defaultConfig(TX_MODE);
  cfg.channels = CHANNELS;
  cfg.sample_rate = SAMPLE_RATE;
  cfg.bits_per_sample = BIT_DEPTH;
  audio_out.begin(cfg);
  audio_out.setVolume(VOLUME);  // Distorts badly when set to 100% volume
}

void setup_network() {
  WiFi.mode(WIFI_STA);
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  WiFi.begin(WIFI_SSID, WIFI_PSK); 
  int connect_failure = 0;
  while (!client.connect(SERVER, PORT)) {
    Serial.println("Failed to connect");
    delay(250);
    if (connect_failure++ >= 50)
      ESP.restart();
  }
  Serial.println("Connected");
}

void IRAM_ATTR write_pcm() {  // Process PCM data
  uint8_t *data = 0;
  if (received_packets > target_buffer_size) {
    if (is_underrun) {
      for (int i = 0; i < SOUNDCHIP_PREFEED_SIZE; i++) {  // Preload the I2S sound card with a couple frames
        data = pop_ram_chunk();
        audio_out.write(data, CHUNK_SIZE);
        played_packets++;
      }
    }
    is_underrun = false;
    data = pop_ram_chunk();
  }
  if (!is_underrun) {
    audio_out.write(data, CHUNK_SIZE);
    played_packets++;
  }
}

void IRAM_ATTR tcp_handler(void *) {
  while (client.connected()) {
    if (client.available() >= CHUNK_SIZE + HEADER_SIZE) {
      client.readBytes(in_buffer, CHUNK_SIZE + HEADER_SIZE);
      uint64_t *new_packet_id = (uint64_t *)(in_buffer + 5);
      if (*new_packet_id != last_packet_id + 1) {
        Serial.print("Got non-sequential packet ids ");
        Serial.print(last_packet_id);
        Serial.print(" -> ");
        Serial.println(*new_packet_id);
      }
      last_packet_id = *new_packet_id;
      if (push_chunk(in_buffer + HEADER_SIZE)) {
        if (received_packets <= target_buffer_size)
          received_packets++;
      } else {
        Serial.println("Buffer overflow, dropping some audio.");
        packet_buffer_size = target_buffer_size;
      }
    }
    delay(1);
  }
  ESP.restart();
}

#ifdef USE_RAM_BUFFER
void IRAM_ATTR buffer_feeder(void *) {
  while(true) {
    buffer_load();
    delay(1);
  }
}
#endif

void setup() {  // Set up I2S sound card and network
  Serial.begin(115200);
  setup_audio_buffers();
  setup_audio();
  setup_network();
  xTaskCreatePinnedToCore(tcp_handler, "tcp_handler", 2048, NULL, 1, NULL, 1);
#ifdef USE_RAM_BUFFER
  xTaskCreatePinnedToCore(buffer_feeder, "buffer_feeder", 2048, NULL, 0, NULL, 0);
#endif
}

void IRAM_ATTR loop() {
  if (!digitalRead(audio_out.getKey(1)))
    sleep();
  if (audio_out.available() > CHUNK_SIZE * 2)
    write_pcm();
  delay(1);
}