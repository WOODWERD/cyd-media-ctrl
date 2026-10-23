#pragma once
#include <Arduino.h>
#define VSPI 3
struct TS_Point { int16_t x = 0, y = 0, z = 0; };
class SPIClass { public: SPIClass(int) {} void begin(int, int, int, int) {} };
class XPT2046_Touchscreen { public: XPT2046_Touchscreen(int, int) {} bool begin(SPIClass&) { return true; } bool touched() { return false; } TS_Point getPoint() { return {}; } void setRotation(int) {} };
