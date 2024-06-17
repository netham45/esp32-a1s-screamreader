#include "global.hpp"
#include "buffer.hpp"
#include "AudioTools.h"                  // Needs https://github.com/pschatzmann/arduino-audio-tools, https://github.com/pschatzmann/arduino-audio-driver
#include "AudioLibs/AudioBoardStream.h"

// Type of audio board being used
audio_driver::AudioBoard I2S_AUDIO_TYPE = AudioKitEs8388V1;
// Channels for incoming PCM, non-configurable (Only implements stereo)
const uint8_t  CHANNELS                 = 2;
 // Samples per chunk
const uint16_t SAMPLES_PER_CHUNK        = (PCM_CHUNK_SIZE / CHANNELS / (BIT_DEPTH / 8));

AudioBoardStream audio_out(I2S_AUDIO_TYPE);
bool is_silent = true;

void IRAM_ATTR pcm_handler(void*) {
  while (true) {
    if (audio_out.availableForWrite() >= PCM_CHUNK_SIZE) {
      uint8_t *data = pop_chunk();
      if (data) {
        audio_out.write(data, PCM_CHUNK_SIZE); 
        is_silent = false;
      } else if (!is_silent) {
        audio_out.writeSilence(PCM_CHUNK_SIZE * 24);
        is_silent = true;
      }
    }
    delay(1);
  }
}

void sleep() {
  audio_out.setActive(false);
  audio_out.setPAPower(false);
  audio_out.end();
  esp_sleep_enable_ext0_wakeup((gpio_num_t)audio_out.getPinID(PinFunction::HEADPHONE_DETECT), false);
  esp_deep_sleep_start();
}

void IRAM_ATTR process_audio_actions(bool is_startup) {
  if (!audio_out.headphoneStatus())
    sleep();
  if (!is_startup)
    audio_out.processActions();
}

void register_button(int button, void (*action)(bool, int, void *)) {
  audio_out.addAction(audio_out.getKey(button), action);
}

void setup_audio() {
  AudioLogger::instance().begin(Serial, AudioLogger::Info);
  I2SCodecConfig cfg = audio_out.defaultConfig(TX_MODE);
  cfg.channels = CHANNELS;
  cfg.sample_rate = SAMPLE_RATE;
  cfg.bits_per_sample = BIT_DEPTH;
  audio_out.begin(cfg);
  audio_out.setVolume(VOLUME);
  xTaskCreatePinnedToCore(pcm_handler, "pcm_handler", 2048, NULL, 1, NULL, 1);
}