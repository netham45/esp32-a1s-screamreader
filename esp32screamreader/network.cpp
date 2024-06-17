#include "esp_wifi.h"                    // ESP IDF
#include <WiFiManager.h>                 // Needs https://github.com/tzapu/WiFiManager
#include "global.hpp"
#include "buffer.hpp"

//#define USE_TCP // Uncomment to use TCP, comment to use UDP

const uint8_t HEADER_SIZE = 5;                         // Scream Header byte size, non-configurable (Part of Scream)
const uint8_t PACKET_SIZE = PCM_CHUNK_SIZE + HEADER_SIZE;

WiFiClient tcp;                               // PCM TCP handler
AsyncUDP udp;                                 // PCM UDP handler
uint8_t in_buffer[PACKET_SIZE];  // TCP input buffer

void IRAM_ATTR tcp_handler(void *) {
  while (tcp.connected()) {
    if (tcp.available() >= PACKET_SIZE) {
      tcp.readBytes(in_buffer, PACKET_SIZE);
      push_chunk(in_buffer + HEADER_SIZE);
    }
    delay(1);
  }
  ESP.restart();
}

void setup_tcp() {
  Serial.println("Connecting to ScreamRouter");
  int connect_failure = 0;
  while (!tcp.connect(SERVER, PORT)) {
    Serial.println("Failed to connect");
    delay(250);
    if (connect_failure++ >= 50)
      ESP.restart();
  }
  Serial.println("Connected to ScreamRouter");
  xTaskCreatePinnedToCore(tcp_handler, "tcp_handler", 2048, NULL, 1, NULL, 0);
}

void setup_udp() {
  udp.listen(PORT);
  udp.onPacket([](AsyncUDPPacket packet) {
    push_chunk(packet.data() + HEADER_SIZE);
  });
}

void setup_network() {
  WiFiManager wm;
  wm.WiFiManagerInit();
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  if (wm.autoConnect("ESP32 ScreamReader Configuration", "SCREAMROUTER")) {
    Serial.print("Wifi Connected to network ");
    Serial.println(wm.getWiFiSSID());
  } else {
    Serial.println("Failed to connect to wifi");
    ESP.restart();
  }
#ifdef USE_TCP
  setup_tcp();
#else
  setup_udp();
#endif

}
