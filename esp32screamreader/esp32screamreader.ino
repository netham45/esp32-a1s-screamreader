// This implements a 16-bit 48kHz stereo TCP PCM receiver on an esp32-a1s audio kit
// https://docs.ai-thinker.com/en/esp32-audio-kit
// Written by Netham45

// Board Library: esp32 by Espressif Systems Version 3.0.1
// Board Library URL: https://espressif.github.io/arduino-esp32/package_esp32_dev_index.json
// Tools -> Board: ESP32 Dev Module

#include "buffer.hpp"
#include "api.hpp"
#include "network.hpp"
#include "audio.hpp"

void setup() {  // Set up I2S sound card and network
  Serial.begin(115200);
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0)  // Restart on interrupt wakeup or the network is unreliable
    ESP.restart();
  setup_buffer();
  setup_audio();
  process_audio_actions(true);
  setup_network();
  setup_api();
}

void IRAM_ATTR loop() {
  process_audio_actions(false);
  delay(1);
}