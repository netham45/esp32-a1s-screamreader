# esp32-audiokit-screamreader

This is an implementation of ScreamReader for the ESP32 A1S AudioKit boards -- https://docs.ai-thinker.com/en/esp32-audio-kit .

I've also made a version that accepts data over TCP, support will be added to ScreamRouter for it soon. TCP can be toggled by uncommenting the TCP define in network.cpp.

It is built using Arduino targeting Arduino ESP32 core version 3.0.1.

It requires https://github.com/pschatzmann/arduino-audio-driver and https://github.com/pschatzmann/arduino-audio-tools as supporting libraries.