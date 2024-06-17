#include "esp_wifi.h"                    // ESP IDF
#include <WiFiManager.h>                 // Needs https://github.com/tzapu/WiFiManager
#include "global.hpp"
#include "buffer.hpp"

bool use_tcp = false; // Uncomment to use TCP, comment to use UDP

const uint16_t HEADER_SIZE = 5;                         // Scream Header byte size, non-configurable (Part of Scream)
const uint16_t PACKET_SIZE = PCM_CHUNK_SIZE + HEADER_SIZE;

WiFiClient tcp;                               // PCM TCP handler
WiFiUDP udp;                                  // PCM UDP handler

void IRAM_ATTR tcp_handler(void *) {
  uint8_t in_buffer[PACKET_SIZE];  // TCP input buffer
  int connect_failure = 0;
  Serial.println("Connecting to ScreamRouter");
  while (!tcp.connect(SERVER, PORT)) {
    Serial.println("Failed to connect");
    delay(250);
    if (connect_failure++ >= 50)
      ESP.restart();
  }
  Serial.println("Connected to ScreamRouter");
  while (tcp.connected()) {
    if (tcp.available() >= PACKET_SIZE) {
      tcp.readBytes(in_buffer, PACKET_SIZE);
      push_chunk(in_buffer + HEADER_SIZE);
    }
    delay(1);
  }
  ESP.restart();
}

void IRAM_ATTR udp_handler(void *) {
  uint8_t in_buffer[PACKET_SIZE];  // TCP input buffer
  udp.begin(PORT);
  while (true) {
    int bytes = udp.parsePacket();
    if (bytes >= PACKET_SIZE) {
      udp.read(in_buffer, PACKET_SIZE);
      push_chunk(in_buffer + HEADER_SIZE);
    }
    delay(1);
  }
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
  if (use_tcp)
    xTaskCreatePinnedToCore(tcp_handler, "tcp_handler", PACKET_SIZE + 2048, NULL, 1, NULL, 0);
  else
    xTaskCreatePinnedToCore(udp_handler, "udp_handler", PACKET_SIZE + 2048, NULL, 1, NULL, 0);
}
