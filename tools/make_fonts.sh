#!/usr/bin/env bash
# Generate the LVGL fonts for Media Ctrl from Inter and JetBrains Mono (OFL, via @fontsource on npm).
# Run from tools/:  npm install lv_font_conv @fontsource/inter @fontsource/jetbrains-mono && ./make_fonts.sh
set -e
F=node_modules/@fontsource
OUT=../src/assets
CONV=node_modules/.bin/lv_font_conv
# Text that comes from the iPad (titles, artists, app names): Latin + Latin Extended-A + common punctuation
RICH="0x20-0x7E,0xA0-0xFF,0x131,0x152-0x153,0x2013-0x2014,0x2018-0x201E,0x2022,0x2026,0x2039-0x203A,0x2212,0x20AC"
RICH_EXT="0x100-0x130,0x132-0x151,0x154-0x17F"
PLAIN="0x20-0x7E,0xB7,0x2013,0x2014,0x2019,0x2026,0x203A,0x2212"

mk() {  # name size font-stem range weight [ext]
  local name=$1 size=$2 stem=$3 range=$4 EXTRA=""
  [ -n "$6" ] && EXTRA="--font $F/$stem-latin-ext-$5-normal.woff --range $RICH_EXT"
  $CONV --bpp 4 --size $size --no-compress --format lvgl --lv-include lvgl.h \
    --font $F/$stem-latin-$5-normal.woff --range $range \
    $EXTRA \
    -o $OUT/$name.c --lv-font-name $name 2>&1 
}
mk font_inter_22sb 22 inter/files/inter "$RICH" 600 ext
mk font_inter_16sb 16 inter/files/inter "$PLAIN" 600
mk font_inter_13sb 13 inter/files/inter "$RICH" 600 ext
mk font_inter_13m  13 inter/files/inter "$RICH" 500 ext
mk font_inter_12r  12 inter/files/inter "$PLAIN" 400
mk font_mono_10    10 jetbrains-mono/files/jetbrains-mono "$PLAIN" 500
ls -la $OUT/font_*.c
