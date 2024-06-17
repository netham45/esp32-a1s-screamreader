#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <string>
#include "global.hpp"
#include "audio.hpp"

std::string current_source = "Firefox";

DynamicJsonDocument get_sources() {
  HTTPClient http;
  DynamicJsonDocument result(32768);
  char url[64];
  sprintf(url, "https://%s/sources", SERVER);
  http.begin(url);
  int response_code = http.GET();
  if (response_code <= 0) {
    Serial.print("Error code: ");
    Serial.println(response_code);
  }
  else {
    auto error = deserializeJson(result, http.getString());
    if (error) {
      Serial.print(F("deserializeJson() failed with code "));
      Serial.println(error.c_str());
    }
  }
  http.end();
  return result;
}

void next_track(bool, int, void*) {
  HTTPClient http;
  char url[64];
  sprintf(url, "https://%s/sources/%s/nexttrack", SERVER, current_source.c_str());
  Serial.println(url);
  http.begin(url);
  http.GET();
  http.end();
}

void prev_track(bool, int, void*) {
  HTTPClient http;
  char url[64];
  sprintf(url, "https://%s/sources/%s/prevtrack", SERVER, current_source.c_str());
  Serial.println(url);
  http.begin(url);
  http.GET();
  http.end();
}

void play_pause(bool, int, void*) {
  HTTPClient http;
  char url[64];
  sprintf(url, "https://%s/sources/%s/play", SERVER, current_source.c_str());
  Serial.println(url);
  http.begin(url);
  http.GET();
  http.end();
}

void rotate_source(bool, int, void*) {
  DynamicJsonDocument sources = get_sources();
  const char* first_source = 0;
  bool found = false;
  bool set = false;
  for (JsonVariant source : sources.as<JsonArray>()) {
    const char* name = source["name"];
    if (found) {
      current_source = name;
      set = true;
      break;
    }
    if (!first_source)
      first_source = name;
    if (strcmp(name, current_source.c_str()) == 0)
      found = true;
  }
  if (!set)
    current_source = first_source;
  Serial.print("New source: ");
  Serial.println(current_source.c_str());
}

void setup_api() {
  register_button(1, rotate_source);
  //register_button(2,);
  //register_button(3,);
  register_button(4, prev_track);
  register_button(5, play_pause);
  register_button(6, next_track);
}