#pragma once
#include <Arduino.h>
class Preferences { public: bool begin(const char*, bool) { return true; } uint8_t getUChar(const char*, uint8_t d) { return d; } size_t putUChar(const char*, uint8_t v) { return 1; } };
