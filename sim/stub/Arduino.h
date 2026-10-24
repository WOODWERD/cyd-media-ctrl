#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <algorithm>
extern uint32_t g_millis;
inline uint32_t millis() { return g_millis; }
inline void delay(uint32_t ms) { g_millis += ms; }
struct SerialStub {
  std::string in; size_t pos = 0; std::string out;
  template<class... A> void printf(const char* f, A... a) { char b[512]; snprintf(b, sizeof b, f, a...); out += b; ::fputs(b, stdout); }
  void println(const char* s) { out += s; out += "\n"; ::puts(s); }
  void begin(int) {} void setRxBufferSize(int) {}
  int available() { return (int)(in.size() - pos); }
  int read() { return pos < in.size() ? (unsigned char)in[pos++] : -1; }
};
extern SerialStub Serial;
#define LOW 0
#define HIGH 1
#define OUTPUT 1
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline void ledcSetup(int, int, int) {}
inline void ledcAttachPin(int, int) {}
inline void ledcWrite(int, int) {}
inline long map(long x, long a, long b, long c, long d) { return (x - a) * (d - c) / (b - a) + c; }
#define constrain(v, lo, hi) ((v) < (lo) ? (lo) : (v) > (hi) ? (hi) : (v))
class String { std::string s; public: String(const char* c = "") : s(c) {} const char* c_str() const { return s.c_str(); } };
struct EspStub { uint32_t getFreeHeap() { return 123456; } void restart() {} };
extern EspStub ESP;
