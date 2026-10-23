#pragma once
#include <Arduino.h>
#define TFT_BLACK 0
extern uint16_t g_fb[320 * 240]; extern int g_w, g_h;
class TFT_eSPI { int ax, ay, aw, ah; public:
  TFT_eSPI(int w, int h) { g_w = w; g_h = h; }
  void init() {} void setRotation(int) {} void setSwapBytes(bool) {} void fillScreen(int) {}
  void startWrite() {} void endWrite() {}
  void setAddrWindow(int x, int y, int w, int h) { ax = x; ay = y; aw = w; ah = h; }
  void pushPixels(uint16_t* p, uint32_t n) { for (uint32_t i = 0; i < n; i++) { int x = ax + i % aw, y = ay + i / aw; if (x < g_w && y < g_h) g_fb[y * g_w + x] = p[i]; } }
};
