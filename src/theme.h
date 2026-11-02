// Media Ctrl — visual tokens, WOODWERD Digital dark theme. Type sizes wait on the device-tier decision.
#pragma once
#include <lvgl.h>

#define C_BG          0x141311   // ground
#define C_SURFACE     0x1C1A17   // button rest
#define C_SURFACE_HI  0x2A2C2F   // pressed
#define C_LINE        0x2A2C2F   // borders, empty bars
#define C_LINE_2      0x3A3D41   // inactive dots, outlines
#define C_TEXT        0xECEAE6
#define C_TEXT_2      0xA3A8AD   // secondary
#define C_TEXT_DIM    0x5D6267   // disabled glyphs
#define C_ACCENT      0xC4944A   // amber: marks, and the play button by recorded exception
#define C_ACCENT_DOWN 0xA87830   // play pressed
#define C_ON_ACCENT   0x141311   // glyphs on the accent
#define C_TINT        0x3A2E16   // quiet accent fill, no status
#define C_WARN        0xFF9F43   // attention: not connected / current pairing step
#define C_OK          0x2F9E5F   // connected, active
#define C_FAULT       0xD0453A
#define C_FAULT_TEXT  0xFF8F85

#define R_BIG   13
#define R_SMALL 8

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
