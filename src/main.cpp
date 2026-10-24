// Media Ctrl — a Bluetooth media remote for the iPad on the Cheap Yellow Display (ESP32-2432S028R).
// WOODWERD LLC · MIT licence.
//
// Portrait, 240 × 320. Pairs with the iPad as a Bluetooth media-key device and, through Apple Media
// Service, shows what is playing (app, title, artist, progress, volume) and sends commands to it.
//   Page 1  NOW PLAYING   title · artist · progress · PREV / PLAY-PAUSE / NEXT · volume
//   Page 2  MORE          −15 s · +15 s · shuffle · repeat · like · mute          (swipe left)
// Long-press the title for About: Bluetooth address, flip screen, forget pairing.
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

#define FW_VERSION "0.2.0"
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
static lv_obj_t *btnVolDn, *btnVolUp, *lblVolVal, *volFill, *pgDot[2];
struct Tile { lv_obj_t *btn, *ico, *lbl; Media::Cmd cmd; };
static Tile tiles[6];
static lv_obj_t *stepNum[3], *stepTxt[3], *lblPairHead, *lblPairFoot;
static lv_obj_t *lblAboutAddr, *lblAboutLink, *btnForget, *lblForget;
static lv_obj_t *toast, *toastTxt;
static uint32_t toastUntil = 0;
static int page = 0;
static uint32_t swallowUntil = 0;    // a swipe just happened: ignore the click it ends with
static uint32_t forgetArmedAt = 0;
static Media::NowPlaying np;
static uint32_t shownRev = 0xFFFFFFFF;
static char lastTitle[160] = "";

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
static lv_obj_t* page_box(lv_obj_t* parent) {
  lv_obj_t* p = box(parent, 0, 0, SCREEN_W, SCREEN_H, C_BG, 0);
  lv_obj_set_style_bg_opa(p, LV_OPA_TRANSP, 0);
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
  lv_obj_set_style_bg_color(pgDot[0], C(p == 0 ? C_TEXT : C_LINE_2), 0);
  lv_obj_set_style_bg_color(pgDot[1], C(p == 1 ? C_TEXT : C_LINE_2), 0);
}

static void gesture_event(lv_event_t*) {
  lv_dir_t d = lv_indev_get_gesture_dir(lv_indev_active());
  if (!lv_obj_has_flag(pgPair, LV_OBJ_FLAG_HIDDEN) || !lv_obj_has_flag(pgAbout, LV_OBJ_FLAG_HIDDEN)) return;
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
static void vol_zone_event(lv_event_t*) { if (!swallowed()) set_page(page ? 0 : 1); }

static void about_open(lv_event_t*) {
  set_text(lblAboutAddr, Media::address().c_str());
  forgetArmedAt = 0;
  set_text(lblForget, "FORGET PAIRING");
  lv_obj_remove_flag(pgAbout, LV_OBJ_FLAG_HIDDEN);
}
static void about_close(lv_event_t*) { lv_obj_add_flag(pgAbout, LV_OBJ_FLAG_HIDDEN); }
static void pair_another_event(lv_event_t*) {
  Media::pairAnother();
  lv_obj_add_flag(pgAbout, LV_OBJ_FLAG_HIDDEN);
}
static void flip_event(lv_event_t*) {
  rot = rot == 0 ? 2 : 0;
  uiPrefs.putUChar("rot", rot);
  apply_rotation();
}
static void forget_event(lv_event_t*) {
  if (forgetArmedAt && millis() - forgetArmedAt < 4000) {
    set_text(lblForget, "FORGETTING…");
    lv_refr_now(NULL);
    Media::forgetPairing();   // reboots
  }
  forgetArmedAt = millis();
  set_text(lblForget, "TAP AGAIN TO FORGET");
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
  dot = box(scr, 0, 0, 6, 6, C_WARN, 3);
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

  barTrack = box(pgNow, 12, 128, 216, 4, C_LINE, 2);
  barFill = box(barTrack, 0, 0, 0, 4, C_ACCENT, 2);
  lblEl = label(pgNow, "", F_MONO, C_TEXT_2);  lv_obj_set_pos(lblEl, 12, 137);
  lblRem = label(pgNow, "", F_MONO, C_TEXT_2); lv_obj_align(lblRem, LV_ALIGN_TOP_RIGHT, -12, 137);

  btnPrev = button(pgNow, 12, 171, 61, 80, R_BIG);
  icoPrev = icon(btnPrev, &ic_prev, C_TEXT);
  lv_obj_add_event_cb(btnPrev, cmd_event, LV_EVENT_CLICKED, (void*)(intptr_t)Media::CMD_PREV);
  btnPlay = button(pgNow, 80, 171, 80, 80, R_BIG);
  lv_obj_set_style_bg_color(btnPlay, C(C_ACCENT), 0);
  lv_obj_set_style_border_width(btnPlay, 0, 0);
  lv_obj_set_style_bg_color(btnPlay, C(0x25AE5E), LV_STATE_PRESSED);
  icoPlay = icon(btnPlay, &ic_play, C_ON_ACCENT);
  lv_obj_add_event_cb(btnPlay, cmd_event, LV_EVENT_CLICKED, (void*)(intptr_t)Media::CMD_TOGGLE);
  btnNext = button(pgNow, 167, 171, 61, 80, R_BIG);
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
    t.btn = button(pgMore, 12 + (i % 2) * 112, 36 + (i / 2) * 69, 104, 62, R_SMALL);
    t.ico = icon(t.btn, defs[i].ic, C_TEXT); lv_obj_align(t.ico, LV_ALIGN_CENTER, 0, -9);
    t.lbl = label(t.btn, defs[i].name, F_MONO, C_TEXT_2); lv_obj_align(t.lbl, LV_ALIGN_CENTER, 0, 16);
    lv_obj_add_event_cb(t.btn, cmd_event, LV_EVENT_CLICKED, (void*)(intptr_t)t.cmd);
  }
}

static void build_volume(lv_obj_t* scr) {
  btnVolDn = button(scr, 12, 258, 50, 50, R_SMALL);
  icon(btnVolDn, &ic_minus, C_TEXT);
  lv_obj_add_event_cb(btnVolDn, cmd_event, LV_EVENT_CLICKED, (void*)(intptr_t)Media::CMD_VOL_DOWN);
  btnVolUp = button(scr, 178, 258, 50, 50, R_SMALL);
  icon(btnVolUp, &ic_plus, C_TEXT);
  lv_obj_add_event_cb(btnVolUp, cmd_event, LV_EVENT_CLICKED, (void*)(intptr_t)Media::CMD_VOL_UP);

  // middle zone: tap flips pages too (the dots live here)
  lv_obj_t* zone = lv_button_create(scr);
  lv_obj_remove_style_all(zone);
  lv_obj_set_pos(zone, 66, 258); lv_obj_set_size(zone, 108, 50);
  lv_obj_add_flag(zone, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_event_cb(zone, vol_zone_event, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* l = label(zone, "VOL", F_MONO, C_TEXT_2); lv_obj_set_pos(l, 3, 10);
  lblVolVal = label(zone, "", F_MONO, C_TEXT); lv_obj_align(lblVolVal, LV_ALIGN_TOP_RIGHT, -3, 10);
  pgDot[0] = box(zone, 46, 13, 4, 4, C_TEXT, 2);
  pgDot[1] = box(zone, 54, 13, 4, 4, C_LINE_2, 2);
  lv_obj_t* vb = box(zone, 3, 28, 102, 4, C_LINE, 2);
  volFill = box(vb, 0, 0, 0, 4, C_TEXT, 2);
}

static void build_pair(lv_obj_t* scr) {
  pgPair = box(scr, 0, 32, SCREEN_W, SCREEN_H - 32, C_BG, 0);
  lv_obj_t* card = box(pgPair, 12, 4, 216, SCREEN_H - 32 - 16, C_SURFACE, R_BIG);
  lv_obj_set_style_border_width(card, 1, 0); lv_obj_set_style_border_color(card, C(C_LINE), 0);
  lblPairHead = label(card, "Pair an iPad or Mac", F_HEAD, C_TEXT); lv_obj_set_pos(lblPairHead, 14, 16);
  const char* steps[3] = { "Open Bluetooth settings on the iPad or Mac.", "Pick Media Ctrl from the list of devices.", "Confirm with Pair or Connect." };
  for (int i = 0; i < 3; i++) {
    const int y = 58 + i * 58;
    stepNum[i] = box(card, 14, y, 22, 22, C_SURFACE, 11);
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
  lv_obj_t* nm = row(66, "NAME"); lv_label_set_text(nm, BLE_NAME);
  lblAboutAddr = row(84, "ADDRESS");
  lblAboutLink = row(102, "NOW PLAYING");

  lv_obj_t* bp = button(pgAbout, 12, 124, 216, 42, R_SMALL);
  lv_obj_t* lp = label(bp, "PAIR ANOTHER DEVICE", F_TITLE, C_TEXT); lv_obj_center(lp);
  lv_obj_add_event_cb(bp, pair_another_event, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* bf = button(pgAbout, 12, 172, 216, 42, R_SMALL);
  lv_obj_t* lf = label(bf, "FLIP SCREEN", F_TITLE, C_TEXT); lv_obj_center(lf);
  lv_obj_add_event_cb(bf, flip_event, LV_EVENT_CLICKED, nullptr);
  btnForget = button(pgAbout, 12, 220, 216, 42, R_SMALL);
  lv_obj_set_style_border_color(btnForget, C(0x6B2A2A), 0);
  lblForget = label(btnForget, "FORGET PAIRING", F_TITLE, 0xFF8F85); lv_obj_center(lblForget);
  lv_obj_add_event_cb(btnForget, forget_event, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* bc = button(pgAbout, 12, 268, 216, 42, R_SMALL);
  lv_obj_t* lc = label(bc, "CLOSE", F_TITLE, C_TEXT); lv_obj_center(lc);
  lv_obj_add_event_cb(bc, about_close, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_flag(pgAbout, LV_OBJ_FLAG_HIDDEN);
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

  toast = box(scr, 0, 150, 120, 30, C_SURFACE_HI, 15);
  toastTxt = label(toast, "", F_TITLE, C_TEXT); lv_obj_center(toastTxt);
  lv_obj_add_flag(toast, LV_OBJ_FLAG_HIDDEN);

  build_about(scr);
  set_page(0);
}

// ---------------- state → screen ----------------
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
  const bool pairing = !linked;

  // top bar
  lv_obj_set_style_bg_color(dot, C(linked ? C_TEXT : C_WARN), 0);
  set_text(lblPlayer, pairing ? "Media Ctrl" : ams ? (np.player[0] ? np.player : host ? "Mac" : "iPad") : "keys only");
  lv_obj_align(lblPlayer, LV_ALIGN_TOP_RIGHT, -12, 11);
  lv_obj_align_to(dot, lblPlayer, LV_ALIGN_OUT_LEFT_MID, -5, 0);
  set_text(lblTitle, pairing ? "Not connected" : "Media Ctrl");

  // pairing card
  if (pairing) {
    lv_obj_remove_flag(pgPair, LV_OBJ_FLAG_HIDDEN);
    const int cur = np.link == Media::LINK_PAIRING ? 2 : 1;
    const bool another = Media::pairWindow();
    set_text(lblPairHead, another ? "Pair another device" : "Pair an iPad or Mac");
    set_text(lblPairFoot, another ? "The last device stays paused for a minute." : "Nothing to install.");
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
  set_text(lblAboutLink, host ? "Mac helper (USB)" : ams ? "Apple Media Service" : linked ? "media keys only" : "not connected");

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
    set_text(lblArtist, nothing ? (host ? "Start Spotify or Music on the Mac, or press play to resume." : "Start something on the iPad, or press play to resume the last app.")
                                : "Track info comes from iPad and iPhone only. The buttons still work.");
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
  lv_obj_align(lblRem, LV_ALIGN_TOP_RIGHT, -12, 137);

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
  if (ams && np.volume >= 0) {
    char v[8]; snprintf(v, sizeof v, "%d", np.volume);
    set_text(lblVolVal, v);
    lv_obj_set_width(volFill, 102 * np.volume / 100);
  } else { set_text(lblVolVal, "—"); lv_obj_set_width(volFill, 0); }
  lv_obj_align(lblVolVal, LV_ALIGN_TOP_RIGHT, -3, 10);

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
    lv_obj_set_style_bg_color(t.btn, C(active ? C_ACCENT : C_SURFACE), 0);
    lv_obj_set_style_border_color(t.btn, C(active ? C_ACCENT : C_LINE), 0);
    set_icon_color(t.ico, active ? C_ON_ACCENT : C_TEXT);
    lv_obj_set_style_text_color(t.lbl, C(active ? C_ON_ACCENT : C_TEXT_2), 0);
  }

#ifndef LED_QUIET
  led(false, false, pairing && ((millis() / 500) % 4) == 0);   // slow blue blink while waiting to pair
#endif
}

// ---------------- housekeeping ----------------
static void ui_tick(lv_timer_t*) {
  render();
  if (toastUntil && millis() > toastUntil) { lv_obj_add_flag(toast, LV_OBJ_FLAG_HIDDEN); toastUntil = 0; }
  if (forgetArmedAt && millis() - forgetArmedAt > 4000) { forgetArmedAt = 0; set_text(lblForget, "FORGET PAIRING"); }

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
