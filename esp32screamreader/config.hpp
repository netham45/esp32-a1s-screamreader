#pragma once
// TCP port for Scream server data, configurable
const uint16_t  PORT                  = 4010;
// Scream server IP, configurable
constexpr char* SERVER                = "192.168.3.114";

// Number of chunks to be buffered before playback starts, configurable
const uint16_t  INITIAL_BUFFER_SIZE   = 644;
// Number of chunks to add each underflow, configurable
const uint16_t  BUFFER_GROW_STEP_SIZE = 16;
// Max number of chunks to be buffered before packets are dropped, configurable
const uint16_t  MAX_BUFFER_SIZE       = 512;

// Sample rate for incoming PCM, configurable
const uint32_t  SAMPLE_RATE                    = 48000;
// Bit depth for incoming PCM, non-configurable (does not implement 24->32 bit padding)
const uint8_t   BIT_DEPTH                      = 16;
// Volume 0.0f-1.0f, distorts at 100%, configurable
const float     VOLUME                         = 1.0f;
