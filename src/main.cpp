// Media Ctrl — a media remote for iPad, iPhone, Mac and Windows on the Cheap Yellow Display (ESP32-2432S028R).
// WOODWERD LLC · MIT licence.
//
// Portrait, 240 × 320. Pairs with the iPad as a Bluetooth media-key device and, through Apple Media
// Service, shows what is playing (app, title, artist, progress, volume) and sends commands to it.
//   Page 1  NOW PLAYING   title · artist · progress · PREV / PLAY-PAUSE / NEXT · volume
//   Page 2  MORE          −15 s · +15 s · shuffle · repeat · like · mute          (swipe left)
// Long-press the title for About: Bluetooth address, flip screen, forget pairing.
// Long-press the source at the top right for Devices: every reachable host, tap one to show it.
// LVGL 9 + TFT_eSPI + XPT2046 + NimBLE.

#include <Arduino.h>
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <Preferences.h>
#include "theme.h"
#include "icons.h"
#include "ble_media.h"
#include "host_link.h"

#define FW_VERSION "0.2.7"
#ifndef BLE_NAME
#define BLE_NAME "Media Ctrl"
#endif

// ---------------- hardware ----------------
#define SCREEN_W 240
#define SCREEN_H 320
#define XPT_IRQ 36
#define XPT_MOSI 32
#define XPT_MISO 39
#define XPT_CLK 25
#define XPT_CS 33
#define LED_R 4
#define LED_G 16
#define LED_B 17
#define BL_PIN 21
#define BL_CH 0
#define BL_FULL 255
#define BL_DIM 60
#ifndef DIM_AFTER_MS
#define DIM_AFTER_MS 120000   // dim (never off) after 2 min untouched; wakes on touch or a new track
#endif

TFT_eSPI tft = TFT_eSPI(SCREEN_W, SCREEN_H);
SPIClass touchSpi = SPIClass(VSPI);
XPT2046_Touchscreen touch(XPT_CS, XPT_IRQ);
uint16_t tMinX = 300, tMaxX = 3800, tMinY = 300, tMaxY = 3800;
static Preferences uiPrefs;
static uint8_t rot = 2;   // panel rotation: 2 = portrait, USB cable at the bottom (default); 0 = flipped

#define DRAW_BUF_SIZE (SCREEN_W * SCREEN_H / 10 * (LV_COLOR_DEPTH / 8))
static uint8_t* draw_buf;
static uint32_t lastTick = 0;
static int blLevel = BL_FULL;

// ---------------- widgets ----------------
static lv_obj_t *lblTitle, *btnBack, *dot, *lblPlayer;
static lv_obj_t *pgNow, *pgMore, *pgPair, *pgAbout;
static lv_obj_t *lblTrack, *lblArtist, *barTrack, *barFill, *lblEl, *lblRem;
static lv_obj_t *btnPrev, *btnPlay, *btnNext, *icoPrev, *icoPlay, *icoNext;
static lv_obj_t *lblVolVal, *volFill, *volKnob, *lblMore;
struct Tile { lv_obj_t *btn, *ico, *lbl, *dot; Media::Cmd cmd; };
static Tile tiles[6];
static lv_obj_t *stepNum[3], *stepTxt[3], *lblPairHead, *lblPairFoot;
static lv_obj_t *lblAboutAddr, *lblAboutShown, *lblAboutLink, *btnForget, *lblForget;
static lv_obj_t *pgDevices, *lblDevCount;
struct DevRow { lv_obj_t *btn, *name, *state, *dot, *sub; int id; };
static DevRow devRows[3];
static lv_obj_t *toast, *toastTxt;
static uint32_t toastUntil = 0;
static int page = 0;
static uint32_t swallowUntil = 0;    // a swipe just happened: ignore the click it ends with
static uint32_t forgetArmedAt = 0;
static Media::NowPlaying np;
static uint32_t shownRev = 0xFFFFFFFF;
static char lastTitle[160] = "";
#define VOL_W 160
static int volTarget = -1, volSent = -1, volRef = 0;   // the level the slider asked for, -1 = none
static bool volDragging = false;
static uint32_t volUntil = 0;

// ---------------- display / touch glue ----------------
static void flush_cb(lv_display_t* disp, const lv_area_t* a, uint8_t* px) {
  uint32_t w = lv_area_get_width(a), h = lv_area_get_height(a);
  tft.startWrite();
  tft.setAddrWindow(a->x1, a->y1, w, h);
  tft.pushPixels((uint16_t*)px, w * h);
  tft.endWrite();
  lv_display_flush_ready(disp);
}

static void touch_read(lv_indev_t*, lv_indev_data_t* data) {
  if (touch.touched()) {
    TS_Point p = touch.getPoint();
    if (p.x < tMinX) tMinX = p.x; if (p.x > tMaxX) tMaxX = p.x;
    if (p.y < tMinY) tMinY = p.y; if (p.y > tMaxY) tMaxY = p.y;
    data->point.x = constrain(map(p.x, tMinX, tMaxX, 0, SCREEN_W - 1), 0, SCREEN_W - 1);
    data->point.y = constrain(map(p.y, tMinY, tMaxY, 0, SCREEN_H - 1), 0, SCREEN_H - 1);
    data->state = LV_INDEV_STATE_PRESSED;
#ifdef TOUCH_DEBUG
    Serial.printf("touch raw %d,%d -> %d,%d\n", p.x, p.y, (int)data->point.x, (int)data->point.y);
#endif
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

static void led(bool r, bool g, bool b) {  // active low
  digitalWrite(LED_R, r ? LOW : HIGH);
  digitalWrite(LED_G, g ? LOW : HIGH);
  digitalWrite(LED_B, b ? LOW : HIGH);
}
static void bl_set(int v) { ledcWrite(BL_CH, v); blLevel = v; }

static void apply_rotation() {
  tft.setRotation(rot);
  touch.setRotation(rot);
  tMinX = 300; tMaxX = 3800; tMinY = 300; tMaxY = 3800;
  lv_obj_invalidate(lv_screen_active());
}

// ---------------- widget helpers ----------------
static lv_obj_t* box(lv_obj_t* parent, int x, int y, int w, int h, uint32_t color, int radius) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_pos(o, x, y); lv_obj_set_size(o, w, h);
  lv_obj_set_style_bg_color(o, C(color), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(o, radius, 0);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(o, LV_OBJ_FLAG_GESTURE_BUBBLE);
  return o;
}
static lv_obj_t* mark(lv_obj_t* parent, int x, int y, int w, int h, uint32_t color, int radius) {   // never takes a touch
  lv_obj_t* o = box(parent, x, y, w, h, color, radius);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
  return o;
}
static lv_obj_t* page_box(lv_obj_t* parent) {
  lv_obj_t* p = box(parent, 0, 0, SCREEN_W, SCREEN_H, C_BG, 0);
  lv_obj_set_style_bg_opa(p, LV_OPA_TRANSP, 0);
  lv_obj_remove_flag(p, LV_OBJ_FLAG_CLICKABLE);   // sits above the title: must not take its long-press
  return p;
}
static lv_obj_t* label(lv_obj_t* parent, const char* s, const lv_font_t* f, uint32_t color) {
  lv_obj_t* l = lv_label_create(parent);
  lv_label_set_text(l, s);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, C(color), 0);
  lv_obj_add_flag(l, LV_OBJ_FLAG_GESTURE_BUBBLE);
  return l;
}
static void set_text(lv_obj_t* l, const char* s) {
  if (strcmp(lv_label_get_text(l), s) != 0) lv_label_set_text(l, s);
}
static lv_obj_t* icon(lv_obj_t* parent, const lv_image_dsc_t* src, uint32_t color) {
  lv_obj_t* im = lv_image_create(parent);
  lv_image_set_src(im, src);
  lv_obj_set_style_image_recolor(im, C(color), 0);
  lv_obj_set_style_image_recolor_opa(im, LV_OPA_COVER, 0);
  lv_obj_add_flag(im, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_center(im);
  return im;
}
static void set_icon_color(lv_obj_t* im, uint32_t color) { lv_obj_set_style_image_recolor(im, C(color), 0); }

static lv_obj_t* button(lv_obj_t* parent, int x, int y, int w, int h, int radius) {
  lv_obj_t* b = lv_button_create(parent);
  lv_obj_remove_style_all(b);
  lv_obj_set_pos(b, x, y); lv_obj_set_size(b, w, h);
  lv_obj_set_style_bg_color(b, C(C_SURFACE), 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(b, C(C_SURFACE_HI), LV_STATE_PRESSED);
  lv_obj_set_style_radius(b, radius, 0);
  lv_obj_set_style_border_width(b, 1, 0);
  lv_obj_set_style_border_color(b, C(C_LINE), 0);
  lv_obj_add_flag(b, LV_OBJ_FLAG_GESTURE_BUBBLE);
  return b;
}
static lv_obj_t* ink_button(lv_obj_t* parent, int x, int y, int w, int h, const char* text) {
  lv_obj_t* b = button(parent, x, y, w, h, R_SMALL);
  lv_obj_set_style_bg_color(b, C(C_TEXT), 0);
  lv_obj_set_style_bg_color(b, C(C_TEXT_2), LV_STATE_PRESSED);
  lv_obj_set_style_border_width(b, 0, 0);
  lv_obj_t* l = label(b, text, F_TITLE, C_ON_ACCENT); lv_obj_center(l);
  return b;
}
static lv_obj_t* outline_button(lv_obj_t* parent, int x, int y, int w, int h, const char* text, lv_obj_t** lbl = nullptr) {
  lv_obj_t* b = button(parent, x, y, w, h, R_SMALL);
  lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_color(b, C(C_LINE_2), 0);
  lv_obj_t* l = label(b, text, F_TITLE, C_TEXT); lv_obj_center(l);
  if (lbl) *lbl = l;
  return b;
}
static void set_enabled(lv_obj_t* b, bool on) {
  if (on) lv_obj_remove_state(b, LV_STATE_DISABLED); else lv_obj_add_state(b, LV_STATE_DISABLED);
}

static void show_toast(const char* s, uint32_t ms = 1200) {
  lv_label_set_text(toastTxt, s);
  lv_obj_update_layout(toast);
  lv_obj_set_width(toast, lv_obj_get_width(toastTxt) + 32);
  lv_obj_align(toast, LV_ALIGN_TOP_MID, 0, 150);
  lv_obj_remove_flag(toast, LV_OBJ_FLAG_HIDDEN);
  toastUntil = millis() + ms;
}

static void fmt_time(char* out, size_t n, float s, bool minus) {
  if (s < 0) s = 0;
  uint32_t t = (uint32_t)(s + 0.5f);
  if (t >= 3600) snprintf(out, n, "%s%lu:%02lu:%02lu", minus ? "−" : "", (unsigned long)(t / 3600), (unsigned long)(t / 60 % 60), (unsigned long)(t % 60));
  else snprintf(out, n, "%s%lu:%02lu", minus ? "−" : "", (unsigned long)(t / 60), (unsigned long)(t % 60));
}

// ---------------- pages ----------------
static void set_page(int p) {
  page = p;
  if (p == 0) { lv_obj_remove_flag(pgNow, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(pgMore, LV_OBJ_FLAG_HIDDEN); }
  else        { lv_obj_add_flag(pgNow, LV_OBJ_FLAG_HIDDEN);    lv_obj_remove_flag(pgMore, LV_OBJ_FLAG_HIDDEN); }
  if (p == 0) { lv_obj_remove_flag(lblTitle, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(btnBack, LV_OBJ_FLAG_HIDDEN); }
  else        { lv_obj_add_flag(lblTitle, LV_OBJ_FLAG_HIDDEN);    lv_obj_remove_flag(btnBack, LV_OBJ_FLAG_HIDDEN); }
  set_text(lblMore, p == 0 ? "More" : "Back");
}

static void gesture_event(lv_event_t*) {
  lv_dir_t d = lv_indev_get_gesture_dir(lv_indev_active());
  if (!lv_obj_has_flag(pgPair, LV_OBJ_FLAG_HIDDEN) || !lv_obj_has_flag(pgAbout, LV_OBJ_FLAG_HIDDEN) ||
      !lv_obj_has_flag(pgDevices, LV_OBJ_FLAG_HIDDEN)) return;
  if (d == LV_DIR_LEFT && page == 0) { set_page(1); swallowUntil = millis() + 400; }
  else if (d == LV_DIR_RIGHT && page == 1) { set_page(0); swallowUntil = millis() + 400; }
}
static bool swallowed() { return (int32_t)(millis() - swallowUntil) < 0; }

// ---------------- actions ----------------
static void cmd_event(lv_event_t* e) {
  if (swallowed()) return;
  Media::Cmd c = (Media::Cmd)(intptr_t)lv_event_get_user_data(e);
  Media::send(c);
  switch (c) {
    case Media::CMD_LIKE:      show_toast("Liked"); break;
    case Media::CMD_MUTE:      show_toast("Mute"); break;
    case Media::CMD_SKIP_BACK: show_toast("Back"); break;
    case Media::CMD_SKIP_FWD:  show_toast("Forward"); break;
    default: break;
  }
}
static void back_event(lv_event_t*) { set_page(0); }
static void more_event(lv_event_t*) { if (!swallowed()) set_page(page ? 0 : 1); }

// The slider (MC-R48 to MC-R50). It holds its own gestures, so a drag never changes page.
static void vol_event(lv_event_t* e) {
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) { volDragging = false; volUntil = millis() + 2000; return; }
  lv_point_t p; lv_indev_get_point(lv_indev_active(), &p);
  lv_area_t a; lv_obj_get_coords((lv_obj_t*)lv_event_get_current_target(e), &a);
  const int level = constrain((p.x - a.x1 - 11) * 100 / (VOL_W - 22), 0, 100);
  if (code == LV_EVENT_PRESSED) { volDragging = true; volRef = level; volSent = -1; }
  volTarget = level; volUntil = millis() + 2000;
}

// Every 200 ms. Helpers take the exact level. AMS and media keys only step, so walk towards it.
static void vol_tick() {
  static uint8_t beat = 0;
  if (volTarget < 0 || (++beat & 1)) return;
  if (np.link == Media::LINK_HOST) {
    if (volTarget != volSent) { Media::setVolume(volTarget); volSent = volTarget; }
  } else if (np.volume >= 0) {
    const int d = volTarget - np.volume;
    if (abs(d) >= 4) Media::send(d > 0 ? Media::CMD_VOL_UP : Media::CMD_VOL_DOWN);
    else if (!volDragging) volTarget = -1;
  } else if (abs(volTarget - volRef) >= 8) {   // no level to follow: one step for each 8 of drag
    const bool up = volTarget > volRef;
    Media::send(up ? Media::CMD_VOL_UP : Media::CMD_VOL_DOWN);
    volRef += up ? 8 : -8;
  }
  if (!volDragging && (int32_t)(millis() - volUntil) > 0) volTarget = -1;
}

static void about_open(lv_event_t*) {
  set_text(lblAboutAddr, Media::address().c_str());
  forgetArmedAt = 0;
  set_text(lblForget, "Forget pairing");
  lv_obj_remove_flag(pgAbout, LV_OBJ_FLAG_HIDDEN);
}
static void about_close(lv_event_t*) { lv_obj_add_flag(pgAbout, LV_OBJ_FLAG_HIDDEN); }
static void devices_open(lv_event_t*) { lv_obj_remove_flag(pgDevices, LV_OBJ_FLAG_HIDDEN); }
static void devices_close(lv_event_t*) { lv_obj_add_flag(pgDevices, LV_OBJ_FLAG_HIDDEN); }
static void device_event(lv_event_t* e) {
  Media::showSource(devRows[(intptr_t)lv_event_get_user_data(e)].id);
  lv_obj_add_flag(pgDevices, LV_OBJ_FLAG_HIDDEN);
  char s[40]; snprintf(s, sizeof s, "Switched to %s", Media::shownName());
  show_toast(s);
}
static void pair_another_event(lv_event_t*) {
  Media::pairAnother();
  lv_obj_add_flag(pgAbout, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(pgDevices, LV_OBJ_FLAG_HIDDEN);
}
static void flip_event(lv_event_t*) {
  rot = rot == 0 ? 2 : 0;
  uiPrefs.putUChar("rot", rot);
  apply_rotation();
}
static void forget_event(lv_event_t*) {
  if (forgetArmedAt && millis() - forgetArmedAt < 4000) {
    set_text(lblForget, "Forgetting…");
    lv_refr_now(NULL);
    Media::forgetPairing();   // reboots
  }
  forgetArmedAt = millis();
  set_text(lblForget, "Tap again to forget");
}

// ---------------- build ----------------
static void build_top(lv_obj_t* scr) {
  lblTitle = label(scr, "Media Ctrl", F_TITLE, C_TEXT);
  lv_obj_set_pos(lblTitle, 12, 9);
  lv_obj_add_flag(lblTitle, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(lblTitle, 10);
  lv_obj_add_event_cb(lblTitle, about_open, LV_EVENT_LONG_PRESSED, nullptr);

  btnBack = lv_button_create(scr);
  lv_obj_remove_style_all(btnBack);
  lv_obj_set_pos(btnBack, 4, 0); lv_obj_set_size(btnBack, 120, 32);
  lv_obj_t* ch = icon(btnBack, &ic_chevron, C_TEXT_2); lv_obj_align(ch, LV_ALIGN_LEFT_MID, 6, 0);
  lv_obj_t* bl = label(btnBack, "Now playing", F_TITLE, C_TEXT_2); lv_obj_align(bl, LV_ALIGN_LEFT_MID, 22, 0);
  lv_obj_add_event_cb(btnBack, back_event, LV_EVENT_CLICKED, nullptr);

  lblPlayer = label(scr, "", F_MONO, C_TEXT_2);
  lv_obj_align(lblPlayer, LV_ALIGN_TOP_RIGHT, -12, 11);
  lv_obj_add_flag(lblPlayer, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(lblPlayer, 18);
  lv_obj_add_event_cb(lblPlayer, devices_open, LV_EVENT_LONG_PRESSED, nullptr);
  dot = mark(scr, 0, 0, 6, 6, C_WARN, 3);
  lv_obj_align_to(dot, lblPlayer, LV_ALIGN_OUT_LEFT_MID, -5, 0);
}

static void build_now(lv_obj_t* scr) {
  pgNow = page_box(scr);
  lblTrack = label(pgNow, "", F_TRACK, C_TEXT);
  lv_obj_set_pos(lblTrack, 12, 41);
  lv_obj_set_width(lblTrack, 216);
  lv_label_set_long_mode(lblTrack, LV_LABEL_LONG_MODE_DOTS);
  lblArtist = label(pgNow, "", F_ARTIST, C_TEXT_2);
  lv_obj_set_width(lblArtist, 216);
  lv_label_set_long_mode(lblArtist, LV_LABEL_LONG_MODE_DOTS);

  barTrack = mark(pgNow, 12, 125, 216, 4, C_LINE, 2);
  barFill = mark(barTrack, 0, 0, 0, 4, C_ACCENT, 2);
  lblEl = label(pgNow, "", F_MONO, C_TEXT_2);  lv_obj_set_pos(lblEl, 12, 135);
  lblRem = label(pgNow, "", F_MONO, C_TEXT_2); lv_obj_align(lblRem, LV_ALIGN_TOP_RIGHT, -12, 135);

  btnPrev = button(pgNow, 12, 158, 62, 80, R_SMALL);
  icoPrev = icon(btnPrev, &ic_prev, C_TEXT);
  lv_obj_add_event_cb(btnPrev, cmd_event, LV_EVENT_CLICKED, (void*)(intptr_t)Media::CMD_PREV);
  btnPlay = button(pgNow, 80, 158, 80, 80, R_SMALL);
  lv_obj_set_style_bg_color(btnPlay, C(C_ACCENT), 0);
  lv_obj_set_style_border_width(btnPlay, 0, 0);
  lv_obj_set_style_bg_color(btnPlay, C(C_ACCENT_DOWN), LV_STATE_PRESSED);
  icoPlay = icon(btnPlay, &ic_play, C_ON_ACCENT);
  lv_obj_add_event_cb(btnPlay, cmd_event, LV_EVENT_CLICKED, (void*)(intptr_t)Media::CMD_TOGGLE);
  btnNext = button(pgNow, 166, 158, 62, 80, R_SMALL);
  icoNext = icon(btnNext, &ic_next, C_TEXT);
  lv_obj_add_event_cb(btnNext, cmd_event, LV_EVENT_CLICKED, (void*)(intptr_t)Media::CMD_NEXT);
}

static void build_more(lv_obj_t* scr) {
  pgMore = page_box(scr);
  struct Def { const lv_image_dsc_t* ic; const char* name; Media::Cmd cmd; };
  const Def defs[6] = {
    {&ic_back15, "−15 s", Media::CMD_SKIP_BACK}, {&ic_fwd15, "+15 s", Media::CMD_SKIP_FWD},
    {&ic_shuffle, "Shuffle", Media::CMD_SHUFFLE}, {&ic_repeat, "Repeat", Media::CMD_REPEAT},
    {&ic_heart, "Like", Media::CMD_LIKE},         {&ic_mute, "Mute", Media::CMD_MUTE},
  };
  for (int i = 0; i < 6; i++) {
    Tile& t = tiles[i];
    t.cmd = defs[i].cmd;
    t.btn = button(pgMore, 12 + (i % 2) * 111, 36 + (i / 2) * 72, 105, 66, R_SMALL);
    t.dot = mark(t.btn, 91, 8, 6, 6, C_OK, 3);
    t.ico = icon(t.btn, defs[i].ic, C_TEXT); lv_obj_align(t.ico, LV_ALIGN_CENTER, 0, -9);
    t.lbl = label(t.btn, defs[i].name, F_MONO, C_TEXT_2); lv_obj_align(t.lbl, LV_ALIGN_CENTER, 0, 16);
    lv_obj_add_event_cb(t.btn, cmd_event, LV_EVENT_CLICKED, (void*)(intptr_t)t.cmd);
  }
}

static void build_volume(lv_obj_t* scr) {
  lv_obj_t* zone = box(scr, 12, 258, VOL_W, 50, C_BG, 0);
  lv_obj_set_style_bg_opa(zone, LV_OPA_TRANSP, 0);
  lv_obj_remove_flag(zone, LV_OBJ_FLAG_GESTURE_BUBBLE);
  for (lv_event_code_t c : {LV_EVENT_PRESSED, LV_EVENT_PRESSING, LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST})
    lv_obj_add_event_cb(zone, vol_event, c, nullptr);
  label(zone, "VOL", F_MONO, C_TEXT_2);
  lblVolVal = label(zone, "", F_MONO, C_TEXT); lv_obj_align(lblVolVal, LV_ALIGN_TOP_RIGHT, 0, 0);
  lv_obj_t* track = mark(zone, 0, 28, VOL_W, 6, C_LINE, 3);
  volFill = mark(track, 0, 0, 0, 6, C_TEXT_2, 3);
  volKnob = mark(zone, 0, 20, 22, 22, C_TEXT, 11);

  lv_obj_t* more = outline_button(scr, 180, 258, 48, 50, "More", &lblMore);
  lv_obj_add_event_cb(more, more_event, LV_EVENT_CLICKED, nullptr);
}

static void build_pair(lv_obj_t* scr) {
  pgPair = box(scr, 0, 32, SCREEN_W, SCREEN_H - 32, C_BG, 0);
  lv_obj_t* card = box(pgPair, 12, 4, 216, SCREEN_H - 32 - 16, C_SURFACE, R_BIG);
  lv_obj_set_style_border_width(card, 1, 0); lv_obj_set_style_border_color(card, C(C_LINE), 0);
  lblPairHead = label(card, "Pair a device", F_HEAD, C_TEXT); lv_obj_set_pos(lblPairHead, 14, 16);
  const char* steps[3] = { "Open Bluetooth settings on your device.", "Pick Media Ctrl from the list of devices.", "Confirm with Pair or Connect." };
  for (int i = 0; i < 3; i++) {
    const int y = 58 + i * 58;
    stepNum[i] = mark(card, 14, y, 22, 22, C_SURFACE, 11);
    lv_obj_set_style_border_width(stepNum[i], 1, 0);
    char n[2] = { (char)('1' + i), 0 };
    lv_obj_t* nl = label(stepNum[i], n, F_MONO, C_TEXT_2); lv_obj_center(nl);
    stepTxt[i] = label(card, steps[i], F_BODY, C_TEXT_2);
    lv_obj_set_width(stepTxt[i], 150); lv_obj_set_pos(stepTxt[i], 46, y + 2);
    lv_label_set_long_mode(stepTxt[i], LV_LABEL_LONG_MODE_WRAP);
  }
  lblPairFoot = label(card, "Nothing to install.", F_MONO, C_TEXT_2);
  lv_obj_set_width(lblPairFoot, 188);
  lv_label_set_long_mode(lblPairFoot, LV_LABEL_LONG_MODE_WRAP);
  lv_obj_align(lblPairFoot, LV_ALIGN_BOTTOM_LEFT, 14, -14);
}

static void build_about(lv_obj_t* scr) {
  pgAbout = box(scr, 0, 0, SCREEN_W, SCREEN_H, C_BG, 0);
  lv_obj_t* h = label(pgAbout, "Media Ctrl", F_HEAD, C_TEXT); lv_obj_set_pos(h, 12, 14);
  lv_obj_t* v = label(pgAbout, "v" FW_VERSION " · WOODWERD", F_MONO, C_TEXT_2); lv_obj_set_pos(v, 12, 38);
  auto row = [&](int y, const char* k) {
    lv_obj_t* kl = label(pgAbout, k, F_MONO, C_TEXT_2); lv_obj_set_pos(kl, 12, y);
    lv_obj_t* vl = label(pgAbout, "", F_MONO, C_TEXT); lv_obj_set_pos(vl, 92, y);
    return vl;
  };
  lblAboutAddr = row(66, "ADDRESS");
  lblAboutShown = row(84, "SHOWING");
  lblAboutLink = row(102, "THROUGH");

  lv_obj_t* bp = outline_button(pgAbout, 12, 124, 216, 42, "Pair a device");
  lv_obj_add_event_cb(bp, pair_another_event, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* bf = outline_button(pgAbout, 12, 172, 216, 42, "Flip screen");
  lv_obj_add_event_cb(bf, flip_event, LV_EVENT_CLICKED, nullptr);
  btnForget = outline_button(pgAbout, 12, 220, 216, 42, "Forget pairing", &lblForget);
  lv_obj_set_style_border_color(btnForget, C(C_FAULT), 0);
  lv_obj_set_style_text_color(lblForget, C(C_FAULT_TEXT), 0);
  lv_obj_add_event_cb(btnForget, forget_event, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* bc = ink_button(pgAbout, 12, 268, 216, 42, "Close");
  lv_obj_add_event_cb(bc, about_close, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_flag(pgAbout, LV_OBJ_FLAG_HIDDEN);
}

static void build_devices(lv_obj_t* scr) {
  pgDevices = box(scr, 0, 0, SCREEN_W, SCREEN_H, C_BG, 0);
  lv_obj_t* h = label(pgDevices, "Devices", F_TITLE, C_TEXT); lv_obj_set_pos(h, 12, 9);
  lblDevCount = label(pgDevices, "", F_MONO, C_TEXT_2); lv_obj_align(lblDevCount, LV_ALIGN_TOP_RIGHT, -12, 11);
  for (int i = 0; i < 3; i++) {
    DevRow& r = devRows[i];
    r.btn = button(pgDevices, 12, 32 + i * 63, 216, 58, R_SMALL);
    lv_obj_add_event_cb(r.btn, device_event, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    r.name = label(r.btn, "", F_TITLE, C_TEXT); lv_obj_set_pos(r.name, 10, 8);
    lv_obj_set_size(r.name, 126, lv_font_get_line_height(F_TITLE)); lv_label_set_long_mode(r.name, LV_LABEL_LONG_MODE_DOTS);
    r.state = label(r.btn, "", F_BODY, C_TEXT_2); lv_obj_align(r.state, LV_ALIGN_TOP_RIGHT, -10, 8);
    r.dot = mark(r.btn, 0, 0, 6, 6, C_OK, 3);
    r.sub = label(r.btn, "", F_MONO, C_TEXT_2); lv_obj_set_pos(r.sub, 10, 32);
    lv_obj_set_width(r.sub, 194); lv_label_set_long_mode(r.sub, LV_LABEL_LONG_MODE_DOTS);
  }
  lv_obj_t* bp = outline_button(pgDevices, 12, 260, 102, 48, "Pair a device");
  lv_obj_add_event_cb(bp, pair_another_event, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* bc = ink_button(pgDevices, 126, 260, 102, 48, "Close");
  lv_obj_add_event_cb(bc, devices_close, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_flag(pgDevices, LV_OBJ_FLAG_HIDDEN);
}

static void ui_build() {
  lv_obj_t* scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, C(C_BG), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(scr, gesture_event, LV_EVENT_GESTURE, nullptr);

  build_top(scr);
  build_now(scr);
  build_more(scr);
  build_volume(scr);
  build_pair(scr);
  build_devices(scr);

  toast = box(scr, 0, 150, 120, 30, C_SURFACE_HI, 15);
  toastTxt = label(toast, "", F_TITLE, C_TEXT); lv_obj_center(toastTxt);
  lv_obj_add_flag(toast, LV_OBJ_FLAG_HIDDEN);

  build_about(scr);
  set_page(0);
}

// ---------------- state → screen ----------------
// "Spotify iPhone" (MC-U13). The app gives way so the device still fits beside the title.
static void source_label(char* out, size_t cap, const char* app, const char* kind, const char* name) {
  if (!app[0]) { strlcpy(out, kind[0] ? kind : name, cap); return; }
  if (!kind[0]) { strlcpy(out, app, cap); return; }
  const int room = 20 - (int)strlen(kind);
  int n = (int)strlen(app) < room ? (int)strlen(app) : room;
  while (n > 0 && ((uint8_t)app[n] & 0xC0) == 0x80) n--;   // never cut a UTF-8 sequence in half
  snprintf(out, cap, "%.*s %s", n, app, kind);
}

static void render_devices() {   // MC-S11, while it's open
  if (lv_obj_has_flag(pgDevices, LV_OBJ_FLAG_HIDDEN)) return;
  Media::Device d[3];
  const int n = Media::devices(d, 3);
  char count[16]; snprintf(count, sizeof count, "%d connected", n);
  set_text(lblDevCount, count);
  lv_obj_align(lblDevCount, LV_ALIGN_TOP_RIGHT, -12, 11);
  for (int i = 0; i < 3; i++) {
    DevRow& r = devRows[i];
    if (i >= n) { lv_obj_add_flag(r.btn, LV_OBJ_FLAG_HIDDEN); continue; }
    lv_obj_remove_flag(r.btn, LV_OBJ_FLAG_HIDDEN);
    r.id = d[i].id;
    set_text(r.name, d[i].name);
    set_text(r.sub, d[i].sub);
    const bool playing = d[i].state == 1;
    set_text(r.state, playing ? "Playing" : d[i].state == 0 ? "Paused" : "Idle");
    lv_obj_set_style_text_color(r.state, C(playing ? C_TEXT : C_TEXT_2), 0);
    lv_obj_align(r.state, LV_ALIGN_TOP_RIGHT, -10, 8);
    lv_obj_align_to(r.dot, r.state, LV_ALIGN_OUT_LEFT_MID, -5, 0);
    lv_obj_set_style_bg_opa(r.dot, playing ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(r.dot, playing ? 0 : 1, 0);
    lv_obj_set_style_border_color(r.dot, C(C_TEXT_2), 0);
    lv_obj_set_style_bg_color(r.btn, C(d[i].shown ? C_TINT : C_SURFACE), 0);
    lv_obj_set_style_border_color(r.btn, C(d[i].shown ? C_ACCENT : C_LINE), 0);
  }
}

static void layout_track_text(const char* title) {
  // title takes one or two lines; the artist line follows it
  lv_point_t sz;
  lv_text_get_size(&sz, title, F_TRACK, 0, 0, 216, LV_TEXT_FLAG_NONE);
  const int line = lv_font_get_line_height(F_TRACK);
  const int h = sz.y > line ? line * 2 : line;
  lv_obj_set_height(lblTrack, h);
  lv_obj_set_pos(lblArtist, 12, 41 + h + 3);
}

static void render() {
  Media::snapshot(np);
  const bool linked = np.link >= Media::LINK_READY;
  const bool host = np.link == Media::LINK_HOST;
  const bool ams = np.link == Media::LINK_AMS || host;   // "rich": we know what is playing
  const bool another = Media::pairWindow();
  const bool pairing = !linked || another;   // the pair window shows even with the helper linked

  // top bar
  lv_obj_set_style_bg_color(dot, C(pairing ? C_WARN : C_OK), 0);
  char source[48];
  source_label(source, sizeof source, np.player, Media::shownKind(), Media::shownName());
  set_text(lblPlayer, pairing ? "Media Ctrl" : ams ? source : "keys only");
  lv_obj_align(lblPlayer, LV_ALIGN_TOP_RIGHT, -12, 11);
  lv_obj_align_to(dot, lblPlayer, LV_ALIGN_OUT_LEFT_MID, -5, 0);
  set_text(lblTitle, another ? "Pairing" : pairing ? "Not connected" : "Media Ctrl");

  // pairing card
  if (pairing) {
    lv_obj_remove_flag(pgPair, LV_OBJ_FLAG_HIDDEN);
    const int cur = np.link == Media::LINK_PAIRING ? 2 : 1;
    set_text(lblPairFoot, another ? "Open for one minute." : "Nothing to install.");
    for (int i = 0; i < 3; i++) {
      const bool on = i == cur;
      lv_obj_set_style_bg_color(stepNum[i], C(on ? C_WARN : C_SURFACE), 0);
      lv_obj_set_style_border_color(stepNum[i], C(on ? C_WARN : C_LINE_2), 0);
      lv_obj_set_style_text_color(lv_obj_get_child(stepNum[i], 0), C(on ? C_ON_ACCENT : C_TEXT_2), 0);
      lv_obj_set_style_text_color(stepTxt[i], C(on ? C_TEXT : C_TEXT_2), 0);
    }
    if (page) set_page(0);
  } else {
    lv_obj_add_flag(pgPair, LV_OBJ_FLAG_HIDDEN);
  }
  char through[24]; snprintf(through, sizeof through, "%s helper (USB)", Media::shownKind());
  set_text(lblAboutLink, host ? through : ams ? "Apple Media Service" : linked ? "media keys only" : "not connected");
  set_text(lblAboutShown, linked ? Media::shownName() : "—");
  render_devices();

  // what is playing
  const bool hasTrack = ams && (np.title[0] || np.artist[0]);
  const bool nothing = ams && !hasTrack;
  const char* title = hasTrack ? (np.title[0] ? np.title : "Untitled") : nothing ? "Nothing playing" : "Connected";
  if (strcmp(title, lastTitle) != 0) {
    strlcpy(lastTitle, title, sizeof lastTitle);
    set_text(lblTrack, title);
    layout_track_text(title);
    lv_display_trigger_activity(NULL);   // new track wakes a dimmed screen
  }
  if (hasTrack) {
    lv_obj_set_style_text_font(lblArtist, F_ARTIST, 0);
    lv_label_set_long_mode(lblArtist, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(lblArtist, lv_font_get_line_height(F_ARTIST));
    set_text(lblArtist, np.artist);
  } else {
    lv_obj_set_style_text_font(lblArtist, F_BODY, 0);
    lv_label_set_long_mode(lblArtist, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_height(lblArtist, LV_SIZE_CONTENT);
    set_text(lblArtist, nothing ? (!host ? "Start something on your device, or press play to resume the last app."
                                       : !strcmp(Media::shownKind(), "Windows") ? "Start something on the PC, or press play to resume."
                                       : "Start Spotify or Music on the Mac, or press play to resume.")
                                : "Track info needs an iPad or iPhone, or the helper on a computer. The buttons still work.");
  }

  // progress
  float el = np.elapsed;
  if (np.state == 1 || np.rate != 0) el += np.rate * (millis() - np.elapsedAt) / 1000.0f;
  if (np.duration > 0 && el > np.duration) el = np.duration;
  const bool timed = hasTrack && np.duration > 0;
  lv_obj_set_width(barFill, timed ? (int)(216 * el / np.duration) : 0);
  if (timed) {
    char a[16], b[16];
    fmt_time(a, sizeof a, el, false); fmt_time(b, sizeof b, np.duration - el, true);
    set_text(lblEl, a); set_text(lblRem, b);
  } else { set_text(lblEl, ""); set_text(lblRem, ""); }
  lv_obj_align(lblRem, LV_ALIGN_TOP_RIGHT, -12, 135);

  // transport
  const bool playing = np.state == 1;
  lv_image_set_src(icoPlay, playing ? &ic_pause : &ic_play);
  const bool canSkip = !nothing && Media::supports(np, Media::CMD_NEXT);
  const bool canBack = !nothing && Media::supports(np, Media::CMD_PREV);
  for (int i = 0; i < 2; i++) {
    lv_obj_t* b = i ? btnNext : btnPrev; lv_obj_t* ic = i ? icoNext : icoPrev; const bool on = i ? canSkip : canBack;
    set_enabled(b, on);
    lv_obj_set_style_bg_opa(b, on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(b, C(on ? C_LINE : C_LINE_2), 0);
    set_icon_color(ic, on ? C_TEXT : C_TEXT_DIM);
  }
  set_enabled(btnPlay, linked);

  // volume
  const int level = volTarget >= 0 ? volTarget : ams ? np.volume : -1;
  const int knob = (level < 0 ? 50 : level) * (VOL_W - 22) / 100;
  char v[8]; snprintf(v, sizeof v, "%d", level);
  set_text(lblVolVal, level < 0 ? "—" : v);
  lv_obj_align(lblVolVal, LV_ALIGN_TOP_RIGHT, 0, 0);
  lv_obj_set_width(volFill, level < 0 ? 0 : knob + 11);
  lv_obj_set_x(volKnob, knob);

  // more page
  for (int i = 0; i < 6; i++) {
    Tile& t = tiles[i];
    const bool sup = Media::supports(np, t.cmd);
    bool active = false;
    if (t.cmd == Media::CMD_SHUFFLE) {
      active = np.shuffle > 0;
      set_text(t.lbl, np.shuffle == 1 ? "Shuffle one" : active ? "Shuffle on" : "Shuffle");
    } else if (t.cmd == Media::CMD_REPEAT) {
      active = np.repeat > 0;
      set_text(t.lbl, np.repeat == 1 ? "Repeat one" : np.repeat == 2 ? "Repeat all" : "Repeat");
    }
    set_enabled(t.btn, sup);
    lv_obj_set_style_opa(t.btn, sup ? LV_OPA_COVER : LV_OPA_40, 0);
    lv_obj_set_style_border_color(t.btn, C(active ? C_OK : C_LINE), 0);
    if (active) lv_obj_remove_flag(t.dot, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(t.dot, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_color(t.lbl, C(active ? C_TEXT : C_TEXT_2), 0);
  }

#ifndef LED_QUIET
  led(false, false, pairing && ((millis() / 500) % 4) == 0);   // slow blue blink while waiting to pair
#endif
}

// ---------------- housekeeping ----------------
static void ui_tick(lv_timer_t*) {
  render();
  vol_tick();
  if (toastUntil && millis() > toastUntil) { lv_obj_add_flag(toast, LV_OBJ_FLAG_HIDDEN); toastUntil = 0; }
  if (forgetArmedAt && millis() - forgetArmedAt > 4000) { forgetArmedAt = 0; set_text(lblForget, "Forget pairing"); }

  const int target = (DIM_AFTER_MS > 0 && lv_display_get_inactive_time(NULL) > DIM_AFTER_MS) ? BL_DIM : BL_FULL;
  if (blLevel != target) {
    int step = target > blLevel ? 60 : -4;
    int next = blLevel + step;
    if ((step > 0 && next > target) || (step < 0 && next < target)) next = target;
    bl_set(next);
  }
}

// ---------------- arduino ----------------
void setup() {
  Serial.setRxBufferSize(2048);   // now-playing lines from the Mac helper can run to ~1 KB
  Serial.begin(115200);
  Serial.println("Media Ctrl v" FW_VERSION);
  pinMode(LED_R, OUTPUT); pinMode(LED_G, OUTPUT); pinMode(LED_B, OUTPUT); led(false, false, false);

  uiPrefs.begin("mediaui", false);
  rot = uiPrefs.getUChar("rot", 2) == 0 ? 0 : 2;   // default: portrait with the USB cable at the bottom

  tft.init();
  tft.setRotation(rot);
  tft.setSwapBytes(true);
  tft.fillScreen(TFT_BLACK);
  ledcSetup(BL_CH, 5000, 8);
  ledcAttachPin(BL_PIN, BL_CH);
  bl_set(BL_FULL);

  touchSpi.begin(XPT_CLK, XPT_MISO, XPT_MOSI, XPT_CS);
  touch.begin(touchSpi);
  touch.setRotation(rot);

  lv_init();
  draw_buf = new uint8_t[DRAW_BUF_SIZE];
  lv_display_t* disp = lv_display_create(SCREEN_W, SCREEN_H);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(disp, flush_cb);
  lv_display_set_buffers(disp, draw_buf, nullptr, DRAW_BUF_SIZE, LV_DISPLAY_RENDER_MODE_PARTIAL);

  lv_indev_t* indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touch_read);

  ui_build();
  render();
  lv_timer_handler();          // first frame before the radio starts
  Media::begin(BLE_NAME);
  HostLink::begin(FW_VERSION);

  lv_timer_create(ui_tick, 100, nullptr);
  lastTick = millis();
}

void loop() {
  lv_tick_inc(millis() - lastTick);
  lastTick = millis();
  lv_timer_handler();
  Media::tick();
  static uint32_t aliveAt = 0;
  if (millis() - aliveAt > 5000) {   // heartbeat for ESP Ctrl's board-health strip
    aliveAt = millis();
    Serial.printf("#alive up=%lu heap=%u\n", (unsigned long)(millis() / 1000), (unsigned)ESP.getFreeHeap());
  }
  delay(5);
}
