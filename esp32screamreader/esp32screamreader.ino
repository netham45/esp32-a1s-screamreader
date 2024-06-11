// This implements a 16-bit 48kHz stereo Scream receiver on an esp32-a1s audio kit
// https://docs.ai-thinker.com/en/esp32-audio-kit
// Written by Netham45

// Board Library: esp32 by Espressif Systems Version 3.0.1
// Board Library URL: https://espressif.github.io/arduino-esp32/package_esp32_dev_index.json
// Tools -> Board: ESP32 Dev Module

#include "AsyncUDP.h"                   // ESP IDF
#include "AudioTools.h"                 // Needs https://github.com/pschatzmann/arduino-audio-tools
#include "AudioLibs/AudioBoardStream.h" // Needs https://github.com/pschatzmann/arduino-audio-driver
#include "wifi_secrets.h"               // In project

#define SAMPLE_RATE 48000               // Sample rate for incoming PCM, configurable
#define BIT_DEPTH 16                    // Bit depth for incoming PCM, non-configurable (does not implement 24->32 bit padding)
#define CHANNELS 2                      // Channels for incoming PCM, non-configurable (Only implements stereo)
#define VOLUME 0.7f                     // Volume 0.0f-1.0f, distorts at 100%, configurable
#define PORT 4010                       // UDP port for incoming Scream data, configurable
#define TARGET_BUFFER_SIZE 4            // Number of chunks to be buffered before playback starts, configurable
#define MAX_BUFFER_SIZE 32              // Max number of chunks to be buffered before packets are dropped, configurable
#define CHUNK_SIZE 1152                 // PCM Bytes per chunk, non-configurable (Part of Scream)
#define HEADER_SIZE 5                   // Scream Header byte size, non-configurable (Part of Scream)
                                        // Samples per chunk
#define SAMPLES_PER_CHUNK (CHUNK_SIZE / CHANNELS / (BIT_DEPTH / 8))
#define I2S_AUDIO_TYPE AudioKitEs8388V1 // Type of audio board being used

AsyncUDP udp;                           // PCM UDP handler
uint8_t received_packets = 0;           // Number of received packets since last buffer fill
uint8_t packet_buffer_size = 0;         // Number of packets in ring buffer
uint8_t packet_buffer_pos = 0;          // Position of ring buffer read head
                                        // Buffer of packets to send
void *packet_buffer[MAX_BUFFER_SIZE] = {0};
                                        // Hardware timer for when PCM should be sent
hw_timer_t *pcm_timer = timerBegin(SAMPLE_RATE);
                                        // Semaphore to know when loop should process PCM
volatile SemaphoreHandle_t timer_semaphore = xSemaphoreCreateBinary();
                                        // Audio Out I2S Stream
AudioBoardStream audio_out(I2S_AUDIO_TYPE);

bool push_chunk(void* chunk) {
  if (packet_buffer_size == MAX_BUFFER_SIZE)
    return false;
  int write_position = (packet_buffer_pos + packet_buffer_size) % MAX_BUFFER_SIZE;
  packet_buffer[write_position] = chunk;
  packet_buffer_size++;
  return true;
}

void* pop_chunk() {
  if (packet_buffer_size == 0)
    return NULL;
  void *return_chunk = packet_buffer[packet_buffer_pos];
  packet_buffer_size--;
  packet_buffer_pos = (packet_buffer_pos + 1) % MAX_BUFFER_SIZE;
  return return_chunk;
}

void setup() {  // Set up I2S sound card and pcm timer and network
  Serial.begin(115200);
  I2SCodecConfig cfg = audio_out.defaultConfig(TX_MODE);
  cfg.channels = CHANNELS;
  cfg.sample_rate = SAMPLE_RATE;
  cfg.bits_per_sample = BIT_DEPTH;
  audio_out.begin(cfg);
  audio_out.setVolume(VOLUME);  // Distorts badly when set to 100% volume
  timerAttachInterrupt(pcm_timer, []() {
    xSemaphoreGiveFromISR(timer_semaphore, NULL);
  });
  timerAlarm(pcm_timer, SAMPLES_PER_CHUNK, true, NULL);
  WiFi.begin(WIFI_SSID, WIFI_PSK);
  udp.onPacket([](AsyncUDPPacket packet) {
    if (packet.length() != CHUNK_SIZE + HEADER_SIZE)
      return;
    void *buffer = memcpy(malloc(CHUNK_SIZE), packet.data() + HEADER_SIZE, CHUNK_SIZE);
    if (!push_chunk(buffer))
      free(buffer);
    else if (received_packets <= TARGET_BUFFER_SIZE)
      received_packets++;
  });
  udp.listen(PORT);
}

void loop() {  // Process PCM data
  if (xSemaphoreTake(timer_semaphore, NULL) == pdTRUE) {
    if (!packet_buffer_size) // Nothing in buffer, fill it back up
      received_packets = 0;
    if (received_packets > TARGET_BUFFER_SIZE) { // Playing back, send data to sound card
      uint8_t* data = (uint8_t*)pop_chunk();
      audio_out.write(data, CHUNK_SIZE);
      free(data);
    } else // Not playing back, send silence to sound card
      audio_out.writeSilence(CHUNK_SIZE);
  }
  else
    delay(2);
}
