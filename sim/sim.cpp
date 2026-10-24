// Host simulator: runs the real firmware UI (src/main.cpp) against fixture now-playing data
// and writes each screen as a PPM, so layouts are checked before anything is flashed.
#include "../src/ble_media.h"
#include "../src/host_link.h"
#include <Arduino.h>
uint32_t g_millis = 1000;
SerialStub Serial;
EspStub ESP;
uint16_t g_fb[320 * 240]; int g_w, g_h;

static Media::NowPlaying fx;
void Media::begin(const char*) {}
void Media::tick() {}
void Media::snapshot(NowPlaying& o) { o = fx; }
void Media::send(Cmd) {}
void Media::forgetPairing() {}
void Media::pairAnother() {}
bool Media::pairWindow() { return false; }
String Media::address() { return String("D7:3A:91:5C:0E:42"); }
void HostLink::begin(const char*) {}
void HostLink::tick() {}
bool HostLink::active() { return false; }
void HostLink::snapshot(Media::NowPlaying&) {}
void HostLink::send(Media::Cmd) {}
void HostLink::handleLine(const char*) {}
bool Media::supports(const NowPlaying& s, Cmd c) {
  if (s.link == LINK_HOST) return c == CMD_MUTE ? (s.supported & (1u << 15)) : (c < 15 && (s.supported & (1u << c)));
  if (c == CMD_MUTE) return s.link >= LINK_READY;
  if (s.link == LINK_AMS) return c < 16 && (s.supported & (1u << c));
  if (s.link == LINK_READY || s.link == LINK_BLIND)
    return c == CMD_TOGGLE || c == CMD_PLAY || c == CMD_PAUSE || c == CMD_NEXT || c == CMD_PREV || c == CMD_VOL_UP || c == CMD_VOL_DOWN;
  return false;
}

#include "../src/main.cpp"

static void dump(const char* name) {
  for (int i = 0; i < 6; i++) { g_millis += 120; lv_tick_inc(120); lv_timer_handler(); }
  lv_refr_now(NULL);
  char path[128]; snprintf(path, sizeof path, "out/%s.ppm", name);
  FILE* f = fopen(path, "wb");
  fprintf(f, "P6\n%d %d\n255\n", g_w, g_h);
  for (int i = 0; i < g_w * g_h; i++) {
    uint16_t p = g_fb[i];
    uint8_t rgb[3] = { (uint8_t)(((p >> 11) & 31) * 255 / 31), (uint8_t)(((p >> 5) & 63) * 255 / 63), (uint8_t)((p & 31) * 255 / 31) };
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
}

int main() {
  setup();
  dump("1_pairing");
  fx.link = Media::LINK_PAIRING; fx.rev++; dump("2_pairing_confirm");

  fx.link = Media::LINK_AMS;
  strcpy(fx.player, "Spotify");
  strcpy(fx.title, "Low Tide Static (Night Version)");
  strcpy(fx.artist, "The Hollow Pines");
  fx.duration = 253; fx.elapsed = 102; fx.rate = 0; fx.state = 0; fx.elapsedAt = g_millis;
  fx.volume = 62; fx.shuffle = 2; fx.repeat = 0;
  fx.supported = 0x0FFF; fx.rev++;
  dump("3_now_playing_paused");
  fx.state = 1; fx.rate = 1; fx.elapsedAt = g_millis; fx.rev++; dump("4_now_playing");
  strcpy(fx.title, "Weightless"); strcpy(fx.artist, "Señor Coconut · Ærø Café — Ünïcødé test"); fx.rev++; dump("5_short_title");
  set_page(1); dump("6_more");
  set_page(0);
  fx.title[0] = fx.artist[0] = 0; fx.state = -1; fx.duration = 0; fx.rev++; dump("7_nothing_playing");
  fx.link = Media::LINK_BLIND; fx.player[0] = 0; fx.rev++; dump("8_keys_only");
  about_open(nullptr); dump("9_about");
  about_close(nullptr);
  // Mac helper over USB
  fx = Media::NowPlaying(); fx.link = Media::LINK_HOST;
  strcpy(fx.player, "Spotify"); strcpy(fx.title, "Low Tide Static, Pt. 2"); strcpy(fx.artist, "The Hollow Pines");
  fx.duration = 253; fx.elapsed = 102.4; fx.state = 1; fx.rate = 1; fx.elapsedAt = g_millis; fx.volume = 62;
  fx.shuffle = 2; fx.repeat = 0; fx.supported = 0x07FF | (1u << 15); fx.rev++;   // no LIKE for Spotify
  dump("10_mac_spotify");
  set_page(1); dump("11_mac_more"); set_page(0);
  fx.title[0] = fx.artist[0] = fx.player[0] = 0; fx.state = -1; fx.duration = 0;
  fx.supported = (1u << Media::CMD_TOGGLE) | (1u << Media::CMD_PLAY) | (1u << Media::CMD_VOL_UP) | (1u << Media::CMD_VOL_DOWN) | (1u << 15); fx.rev++;
  dump("12_mac_idle");
  return 0;
}
