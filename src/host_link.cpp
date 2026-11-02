#include "host_link.h"
#include <ArduinoJson.h>

static const uint32_t HOST_TIMEOUT_MS = 8000;   // helper sends at least every 5 s
static Media::NowPlaying hp;                    // touched only from the main loop
static uint32_t lastSeen = 0;
static bool seen = false;
static const char* fw = "";
static char buf[1536];
static size_t len = 0;
static bool overflow = false;
static bool windows = false;

static void hello() { Serial.printf("{\"t\":\"hello\",\"dev\":\"media-ctrl\",\"fw\":\"%s\"}\n", fw); }

static void clearTrack() {
  hp.player[0] = hp.title[0] = hp.artist[0] = 0;
  hp.duration = hp.elapsed = hp.rate = 0; hp.elapsedAt = millis();
  hp.state = -1; hp.shuffle = -1; hp.repeat = -1;
}

static void copyStr(char* dst, size_t cap, const char* src) {
  if (!src) { dst[0] = 0; return; }
  size_t n = strlen(src);
  if (n >= cap) {           // cut on a UTF-8 boundary
    n = cap - 1;
    while (n > 0 && ((uint8_t)src[n] & 0xC0) == 0x80) n--;
  }
  memcpy(dst, src, n); dst[n] = 0;
}

void HostLink::begin(const char* fwVersion) {
  fw = fwVersion;
  hp.link = Media::LINK_HOST;
  strcpy(hp.player, "");
  hello();
}

void HostLink::handleLine(const char* line) {
  JsonDocument doc;
  if (deserializeJson(doc, line)) return;
  const char* t = doc["t"] | "";
  if (!strcmp(t, "who")) { hello(); return; }
  bool known = true;
  if (!strcmp(t, "np")) {
    copyStr(hp.player, sizeof hp.player, doc["app"] | "");
    copyStr(hp.title, sizeof hp.title, doc["title"] | "");
    copyStr(hp.artist, sizeof hp.artist, doc["artist"] | "");
    hp.duration = doc["dur"] | 0.0f;
    hp.elapsed = doc["pos"] | 0.0f;
    hp.state = doc["state"] | -1;
    hp.rate = hp.state == 1 ? 1.0f : 0.0f;
    hp.elapsedAt = millis();
    hp.volume = doc["vol"] | -1;
    hp.shuffle = doc["shuffle"] | -1;
    hp.repeat = doc["repeat"] | -1;
    uint16_t mask = 0;
    for (JsonVariant v : doc["sup"].as<JsonArray>()) {
      int c = v.as<int>();
      if (c >= 0 && c < 15) mask |= 1u << c;
      else if (c == Media::CMD_MUTE) mask |= 1u << 15;   // mute rides in bit 15
    }
    hp.supported = mask;
  } else if (!strcmp(t, "idle")) {
    clearTrack();
    hp.volume = doc["vol"] | -1;
    uint16_t mask = (1u << Media::CMD_TOGGLE) | (1u << Media::CMD_PLAY) | (1u << Media::CMD_VOL_UP) |
                    (1u << Media::CMD_VOL_DOWN) | (1u << 15);
    hp.supported = mask;
  } else if (!strcmp(t, "host")) {
    windows = !strcmp(doc["os"] | "", "windows");   // a missing os means Mac (MC-H2)
  } else {
    known = false;
  }
  if (known) { lastSeen = millis(); seen = true; hp.link = Media::LINK_HOST; hp.rev++; }
}

void HostLink::tick() {
  while (Serial.available()) {
    int ch = Serial.read();
    if (ch < 0) break;
    if (ch == '\n' || ch == '\r') {
      if (len && !overflow) { buf[len] = 0; if (buf[0] == '{') handleLine(buf); }
      len = 0; overflow = false;
    } else if (len < sizeof buf - 1) {
      buf[len++] = (char)ch;
    } else {
      overflow = true;      // drop the rest of an over-long line
    }
  }
}

const char* HostLink::osName() { return windows ? "Windows" : "Mac"; }

bool HostLink::active() { return seen && (millis() - lastSeen) < HOST_TIMEOUT_MS; }

void HostLink::snapshot(Media::NowPlaying& out) { out = hp; }

void HostLink::send(Media::Cmd c) {
  Serial.printf("{\"t\":\"cmd\",\"c\":%u}\n", (unsigned)c);
}

void HostLink::sendVolume(int level) {
  Serial.printf("{\"t\":\"vol\",\"v\":%d}\n", level);
}
