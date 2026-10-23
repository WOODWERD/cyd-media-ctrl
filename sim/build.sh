#!/usr/bin/env bash
# Build the host simulator and render every screen to sim/out/*.ppm (and PNG at 2x if Pillow is installed).
# Run from the repo root after `pio pkg install -e cyd2usb` (it reuses the downloaded LVGL).
set -e
L=.pio/libdeps/cyd2usb/lvgl
mkdir -p sim/obj sim/out
sed 's/^#define LV_USE_TFT_ESPI .*/#define LV_USE_TFT_ESPI 0/' include/lv_conf.h > sim/stub/lv_conf.h
for s in $(find $L/src -name '*.c' ! -path '*/drivers/*' ! -path '*/libs/*' ! -path '*/osal/*' ! -path '*/debugging/*') \
         $L/src/osal/lv_os.c $L/src/osal/lv_os_none.c $(find $L/src/libs -path '*/bin_decoder/*' -name '*.c') src/assets/*.c; do
  o=sim/obj/$(echo $s | md5sum | cut -c1-12).o
  [ -f $o ] || gcc -c -O1 -w -DLV_CONF_INCLUDE_SIMPLE -Isim/stub -I$L -I$L/src $s -o $o
done
g++ -O1 -w -std=c++17 -DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE -Isim/stub -Isrc -I$L -I$L/src sim/sim.cpp sim/obj/*.o -o sim/sim
(cd sim && ./sim > /dev/null)
python3 -c "
import glob
from PIL import Image
for p in glob.glob('sim/out/*.ppm'): Image.open(p).resize((480,640), Image.NEAREST).save(p[:-4] + '.png')
" 2>/dev/null || true
ls sim/out
