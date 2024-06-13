# esp32-audiokit-screamreader

This is an implementation of ScreamReader for the ESP32 A1S AudioKit boards -- https://docs.ai-thinker.com/en/esp32-audio-kit

The esp32 has pretty bad seemingly unavoidable UDP packet loss. I've also made a version that accepts data over TCP, support will be added to ScreamRouter for it soon.

It is built using Arduino targeting Arduino ESP32 core version 3.0.1.

It requires https://github.com/pschatzmann/arduino-audio-driver and https://github.com/pschatzmann/arduino-audio-tools as supporting libraries.