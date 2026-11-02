# Third-party notices

Media Ctrl itself is © 2026 WOODWERD LLC and MIT-licensed (see `LICENSE`). It builds on, and distributes, the components below.

## Compiled into the firmware (`release/*.bin` and the PlatformIO build)

| Component | License | Notes |
|---|---|---|
| [LVGL](https://github.com/lvgl/lvgl) 9.5 | MIT | UI toolkit. `include/lv_conf.h` is derived from LVGL's template. |
| [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) | FreeBSD (BSD-2), plus BSD portions from Adafruit_GFX | Display driver |
| [XPT2046_Touchscreen](https://github.com/PaulStoffregen/XPT2046_Touchscreen) | MIT | Touch controller |
| [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) 2.x | Apache-2.0 (Apache NimBLE) | Bluetooth LE: HID device and GATT client. Pinned at 2.5.2 and patched at build time by `tools/patch_nimble_two_client.py` so each connection gets its own GATT client (two or three hosts at once). |
| [ArduinoJson](https://github.com/bblanchon/ArduinoJson) 7 | MIT | Parses the Mac helper's messages |
| [arduino-esp32](https://github.com/espressif/arduino-esp32) core | LGPL-2.1 | Arduino core for ESP32. Full source of this project is provided, which satisfies the relinking requirements. |
| [ESP-IDF](https://github.com/espressif/esp-idf) components (bundled by the platform) | Apache-2.0 | |
| [platform-espressif32](https://github.com/platformio/platform-espressif32) | Apache-2.0 | Build platform only; not distributed |

The HID report map in `src/ble_media.cpp` uses the Consumer page of the USB HID Usage Tables, a public specification. The PnP ID values are conventional placeholders and don't claim a registered vendor ID.

The Apple Media Service client is written from Apple's published *Apple Media Service* specification. No Apple code is used.

## Bundled with the Mac helper (`mac-helper/serial/`)

| Component | License | Notes |
|---|---|---|
| [pyserial](https://github.com/pyserial/pyserial) 3.5 | BSD-3-Clause | Copied unmodified, trimmed to the POSIX/macOS modules. Licence in `mac-helper/serial/LICENSE-pyserial.txt`. |

The helper controls Spotify and Music through their public AppleScript dictionaries and uses no code from either.

## Fonts (`src/assets/font_*.c`, converted with `lv_font_conv`)

| Font | License | File |
|---|---|---|
| [Inter](https://github.com/rsms/inter): SemiBold 22/16/13, Medium 13, Regular 12 px | SIL Open Font License 1.1 | `src/assets/LICENSE-Inter-OFL.txt` |
| [JetBrains Mono](https://github.com/JetBrains/JetBrainsMono): Medium 10 px | SIL Open Font License 1.1 | `src/assets/LICENSE-JetBrainsMono-OFL.txt` |

The `.c` files are bitmap conversions of subsets of these fonts, embedded in the firmware. Under the OFL they are Modified Versions: they stay under the OFL, aren't sold on their own, and use the original names only to identify where they came from.

The icons (`src/assets/icons.c`, drawn by `tools/make_icons.js`) are original and MIT-licensed like the rest of the project.

## Hardware references

- Board pin assignments come from [witnessmenow/ESP32-Cheap-Yellow-Display](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display) (`PINS.md`, MIT).
- "Cheap Yellow Display" / CYD is a community nickname for the Sunton ESP32-2432S028 board. No Sunton documentation is redistributed.

## Trademarks

Apple, iPad, iPhone, Mac, macOS, iPadOS and Apple Music are trademarks of Apple Inc. Spotify is a trademark of Spotify AB. YouTube is a trademark of Google LLC. This project is an independent, unofficial accessory that uses standard Bluetooth media keys and Apple's published Apple Media Service. It isn't affiliated with, endorsed by or supported by any of these companies. App names that appear on the screen are supplied by the host device at run time.
