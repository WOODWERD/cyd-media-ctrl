// Media Ctrl — visual tokens (from the approved design canvas, green accent).
#pragma once
#include <lvgl.h>

#define C_BG          0x0B0D10   // ground
#define C_SURFACE     0x16191E   // button rest
#define C_SURFACE_HI  0x22262D   // pressed
#define C_LINE        0x262A31   // borders, empty bars
#define C_LINE_2      0x3A3F48   // inactive dots, outlines
#define C_TEXT        0xECEEF1
#define C_TEXT_2      0x8A919C   // secondary
#define C_TEXT_DIM    0x5B616B   // disabled glyphs
#define C_ACCENT      0x2ED573   // green
#define C_ON_ACCENT   0x0B0D10   // glyphs on the accent
#define C_WARN        0xFFB000   // not connected / current pairing step

#define R_BIG   14
#define R_SMALL 12

LV_FONT_DECLARE(font_inter_22sb);
LV_FONT_DECLARE(font_inter_16sb);
LV_FONT_DECLARE(font_inter_13sb);
LV_FONT_DECLARE(font_inter_13m);
LV_FONT_DECLARE(font_inter_12r);
LV_FONT_DECLARE(font_mono_10);
#define F_TRACK  (&font_inter_22sb)
#define F_HEAD   (&font_inter_16sb)
#define F_TITLE  (&font_inter_13sb)
#define F_ARTIST (&font_inter_13m)
#define F_BODY   (&font_inter_12r)
#define F_MONO   (&font_mono_10)

static inline lv_color_t C(uint32_t hex) { return lv_color_hex(hex); }
