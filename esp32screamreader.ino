// This implements a 16-bit 48kHz stereo Scream receiver on an esp32-a1s audio kit https://docs.ai-thinker.com/en/esp32-audio-kit
#include "AudioTools.h"                 // Needs https://github.com/pschatzmann/arduino-audio-driver
#include "AudioLibs/AudioBoardStream.h" // Needs https://github.com/pschatzmann/arduino-audio-tools
#include "AsyncUDP.h"
#include "wifi_secrets.h"

#define SILENCE_TIME_MS 30  // Time in MS before the buffer gets wiped and the stream is considered dead
#define SAMPLE_RATE 48000 // Sample rate for incoming PCM
#define BIT_DEPTH 16 // Bit depth for incoming PCM
#define CHANNELS 2 // Channels for incoming PCM
#define PORT 4010 // UDP port for incoming Scream data
#define SILENCE_BYTES 11520 // Number of bytes to send to es8388 to empty the buffer

uint64_t last_packet = 0;  // Timestamp of last packet received
bool playing = true;  // If false the es8388 buffer has filled with silence
AsyncUDP udp; // UDP handler
AudioBoardStream kit(AudioKitEs8388V1); // Stream to es8388 to send audio to

uint64_t get_time_ms() { // Get time in milliseconds
  struct timeval tv_now;
  gettimeofday(&tv_now, NULL);
  return (int64_t)tv_now.tv_sec * 1000L + (int64_t)tv_now.tv_usec / 1000;
}

void handle_packet(AsyncUDPPacket packet) {
  kit.write(packet.data() + 5, packet.length() - 5);  // Ignore 5 byte Scream header
  last_packet = get_time_ms();
}

void setup() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PSK);
  I2SCodecConfig cfg = kit.defaultConfig(TX_MODE);
  cfg.channels = CHANNELS;  // Stereo
  cfg.sample_rate = SAMPLE_RATE; // 48kHz
  cfg.bits_per_sample = BIT_DEPTH; // 16-bit depth
  kit.begin(cfg);
  udp.listen(PORT);
  udp.onPacket(handle_packet);
}

void loop() {
  delay(10);
  bool still_playing = get_time_ms() - last_packet < SILENCE_TIME_MS;
  if (!still_playing && playing)
    kit.writeSilence(SILENCE_BYTES);
  playing = still_playing;
}
