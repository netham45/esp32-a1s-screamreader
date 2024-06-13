// This implements a 16-bit 48kHz stereo TCP PCM receiver on an esp32-a1s audio kit
// https://docs.ai-thinker.com/en/esp32-audio-kit
// Written by Netham45

// Board Library: esp32 by Espressif Systems Version 3.0.1
// Board Library URL: https://espressif.github.io/arduino-esp32/package_esp32_dev_index.json
// Tools -> Board: ESP32 Dev Module

#include "esp_wifi.h"                   // ESP IDF
#include "AsyncUDP.h"                   // ESP IDF
#include "AudioTools.h"                 // Needs https://github.com/pschatzmann/arduino-audio-tools
#include "AudioLibs/AudioBoardStream.h" // Needs https://github.com/pschatzmann/arduino-audio-driver
#include "wifi_secrets.h"               // In project

#define SAMPLE_RATE 48000               // Sample rate for incoming PCM, configurable
#define BIT_DEPTH 16                    // Bit depth for incoming PCM, non-configurable (does not implement 24->32 bit padding)
#define CHANNELS 2                      // Channels for incoming PCM, non-configurable (Only implements stereo)
#define VOLUME 1.0f                     // Volume 0.0f-1.0f, distorts at 100%, configurable
#define PORT 4010                       // TCP port for Scream server data, configurable
#define SERVER "192.168.3.114"          // Scream server IP
#define INITIAL_BUFFER_SIZE 64          // Number of chunks to be buffered before playback starts, configurable
#define SOUNDCHIP_PREFEED_SIZE 32       // Number of chunks to be sent to the sound chip once initial buffer is reached, configurable
#define BUFFER_GROW_STEP_SIZE 8         // Number of chunks to add each underflow, configurable
#define MAX_BUFFER_SIZE 128             // Max number of chunks to be buffered before packets are dropped, configurable
#define CHUNK_SIZE 1152                 // PCM Bytes per chunk, non-configurable (Part of Scream)
#define HEADER_SIZE 13                  // Scream Header byte size, non-configurable (Part of Scream)
                                        // Samples per chunk
#define SAMPLES_PER_CHUNK (CHUNK_SIZE / CHANNELS / (BIT_DEPTH / 8))
#define I2S_AUDIO_TYPE AudioKitEs8388V1 // Type of audio board being used

WiFiClient client;                      // PCM TCP handler
uint64_t received_packets = 0;          // Number of received packets since last buffer fill
uint64_t played_packets = 0;          // Number of packets sent to audio card
uint64_t packet_buffer_size = 0;        // Number of packets in ring buffer
uint64_t packet_buffer_pos = 0;         // Position of ring buffer read head
bool is_underrun = true;                // True if currently underrun
                                        // Buffer of packets to send
uint8_t *packet_buffer[MAX_BUFFER_SIZE] = {0};
                                        // Number of bytes to buffer
uint64_t target_buffer_size = INITIAL_BUFFER_SIZE;
                                        // Hardware timer for when PCM should be sent
hw_timer_t *pcm_timer = timerBegin(SAMPLE_RATE);
                                        // Semaphore to know when loop should process PCM
volatile SemaphoreHandle_t timer_semaphore = xSemaphoreCreateBinary();
                                        // Audio Out I2S Stream
uint64_t last_packet_id = 0;            // ID of last packet, temp
                                        // TCP input buffer
uint8_t in_buffer[CHUNK_SIZE + HEADER_SIZE];
AudioBoardStream audio_out(I2S_AUDIO_TYPE);

bool push_chunk(uint8_t* chunk) {
  if (packet_buffer_size == MAX_BUFFER_SIZE)
    return false;
  int write_position = (packet_buffer_pos + packet_buffer_size) % MAX_BUFFER_SIZE;
  memcpy(packet_buffer[write_position], chunk, CHUNK_SIZE);
  packet_buffer_size++;
  return true;
}

uint8_t* pop_chunk() {
  if (packet_buffer_size == 0)
    return NULL;
  uint8_t* return_chunk = packet_buffer[packet_buffer_pos];
  packet_buffer_size--;
  packet_buffer_pos = (packet_buffer_pos + 1) % MAX_BUFFER_SIZE;
  return return_chunk;
}

void sleep() {
  audio_out.setActive(false);
  audio_out.end();
  esp_deep_sleep_start();
}

void setup_audio_buffer() {
  Serial.println("Allocating buffer");
  if (MAX_BUFFER_SIZE > 128)
    psramInit();
  for (int i=0;i<MAX_BUFFER_SIZE;i++)
  {
    if (i > 128)
      packet_buffer[i] = (uint8_t*)ps_malloc(CHUNK_SIZE);
    else
      packet_buffer[i] = (uint8_t*)malloc(CHUNK_SIZE);
    if (packet_buffer[i] == 0)
    {
      Serial.print("Failed to allocate buffer index ");
      Serial.println(i);
    }
  }
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

void setup_interrupt() {
  timerAttachInterrupt(pcm_timer, []() {
    xSemaphoreGiveFromISR(timer_semaphore, NULL);
  });
  timerAlarm(pcm_timer, SAMPLES_PER_CHUNK, true, NULL);
}

void setup_network() {
  WiFi.begin(WIFI_SSID, WIFI_PSK);
  while (!client.connect(SERVER, PORT))
  {
    Serial.println("Failed to connect");
    delay(500);
  }
  Serial.println("Connected");
}

void write_pcm() {  // Process PCM data
  uint8_t* data = 0;
  if (received_packets > target_buffer_size) {
    if (is_underrun) {
      for (int i=0;i<SOUNDCHIP_PREFEED_SIZE;i++) { // Preload the I2S sound card with a couple frames
        data = pop_chunk();
        audio_out.write(data, CHUNK_SIZE);
        played_packets++;
      }
    }
    is_underrun = false;
    data = pop_chunk();
  }
  if (!data) {
    if (!is_underrun) {
      audio_out.writeSilence(CHUNK_SIZE * 25);
      Serial.println("Buffer underflow");
      received_packets = 0;
      played_packets = 0;
      target_buffer_size += BUFFER_GROW_STEP_SIZE;
      if (target_buffer_size > MAX_BUFFER_SIZE)
        target_buffer_size = MAX_BUFFER_SIZE;
    }
    is_underrun = true;
  }
  if (!is_underrun)
  {
    audio_out.write(data, CHUNK_SIZE);
    played_packets++;
  }
}

void setup() {  // Set up I2S sound card and pcm timer and network
  Serial.begin(115200);
  setup_audio_buffer();
  setup_audio();
  setup_interrupt();
  setup_network();
  xTaskCreatePinnedToCore(tcp_handler, "tcp_handler", 2048, NULL, 1, NULL, 1);
}

void tcp_handler(void *) {
  while(client.connected())
  {
    if (client.available() >= CHUNK_SIZE + HEADER_SIZE) {
      client.readBytes(in_buffer, CHUNK_SIZE + HEADER_SIZE);
      uint64_t *new_packet_id = (uint64_t*)(in_buffer + 5);
      if (*new_packet_id != last_packet_id + 1) {
        Serial.print("Got non-sequential packet ids ");
        Serial.print(last_packet_id);
        Serial.print(" -> ");
        Serial.println(*new_packet_id);
      }
      last_packet_id = *new_packet_id;
      if (push_chunk(in_buffer + HEADER_SIZE)) {
        //if (received_packets <= target_buffer_size)
        received_packets++;
      } else {
        Serial.println("Buffer overflow, dropping some audio.");
        packet_buffer_size = target_buffer_size;
      }
    } else
      delay(2);
  }
  ESP.restart();
}

void loop() {
  if (!digitalRead(audio_out.getKey(1)))
    sleep();
  if (xSemaphoreTake(timer_semaphore, NULL) == pdTRUE)
    write_pcm();
  else
    delay(4);
}