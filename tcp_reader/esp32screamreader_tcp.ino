// This implements a 16-bit 48kHz stereo TCP PCM receiver on an esp32-a1s audio kit
// https://docs.ai-thinker.com/en/esp32-audio-kit
// Written by Netham45

// Board Library: esp32 by Espressif Systems Version 3.0.1
// Board Library URL: https://espressif.github.io/arduino-esp32/package_esp32_dev_index.json
// Tools -> Board: ESP32 Dev Module

#include "esp_wifi.h"                    // ESP IDF
#include "AudioTools.h"                  // Needs https://github.com/pschatzmann/arduino-audio-tools
#include "AudioLibs/AudioBoardStream.h"  // Needs https://github.com/pschatzmann/arduino-audio-driver
#include "wifi_secrets.h"                // In project
#include "ringbuf.hpp"

#define SAMPLE_RATE 48000                // Sample rate for incoming PCM, configurable
#define BIT_DEPTH 16                     // Bit depth for incoming PCM, non-configurable (does not implement 24->32 bit padding)
#define CHANNELS 2                       // Channels for incoming PCM, non-configurable (Only implements stereo)
#define VOLUME 0.6f                      // Volume 0.0f-1.0f, distorts at 100%, configurable
#define PORT 4010                        // TCP port for Scream server data, configurable
#define SERVER "192.168.3.114"           // Scream server IP
#define CHUNK_SIZE 1152                  // PCM Bytes per chunk, non-configurable (Part of Scream)
#define HEADER_SIZE 13                   // Scream Header byte size, non-configurable (Part of Scream)
#define MINIMUM_BUFFER 16                // Minimum number of packets before playback starts
#define I2S_AUDIO_TYPE AudioKitEs8388V1  // Type of audio board being used

WiFiClient client;                           // PCM TCP handler
AudioBoardStream audio_out(I2S_AUDIO_TYPE);  // Audio Out
RingBuf ringbuf(CHUNK_SIZE * 128);
StreamCopy copier(audio_out, ringbuf);

uint8_t in_buffer[CHUNK_SIZE + HEADER_SIZE];  // TCP input buffer

void sleep() {
  audio_out.setActive(false);
  audio_out.end();
  esp_deep_sleep_start();
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

void IRAM_ATTR tcp_handler(void *) {
  while (client.connected())
    if (client.available() >= CHUNK_SIZE + HEADER_SIZE && ringbuf.available_to_write() >= CHUNK_SIZE + HEADER_SIZE) {
      client.readBytes(in_buffer, CHUNK_SIZE + HEADER_SIZE);
      ringbuf.writeBytes(in_buffer + HEADER_SIZE, CHUNK_SIZE);
    }
    delay(5);
  ESP.restart();
}

void setup() {  // Set up I2S sound card and network
  Serial.begin(115200);
  ringbuf.begin();
  setup_audio();
  setup_network();
  xTaskCreatePinnedToCore(tcp_handler, "tcp_handler", 2048, NULL, 1, NULL, 1);
}

void IRAM_ATTR loop() {
  if (!digitalRead(audio_out.getKey(1)))
    sleep();
  copier.copy();
}