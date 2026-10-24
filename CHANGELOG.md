# Changelog

## v0.2.0 — 2026-10-23
- **Mac support with full Now Playing.** A new helper app (`mac-helper/`) reads Spotify and Apple Music on the Mac and feeds the board over the USB cable. It also carries button presses back as AppleScript: play/pause, previous/next, ±15 s, shuffle, repeat, like (Music) and system volume/mute. Install with `./install.sh` (LaunchAgent; uses the Mac's built-in Python). See [docs/MAC.md](docs/MAC.md).
- Firmware: a USB host link (JSON lines at 115200, documented in `src/host_link.h`) alongside Bluetooth. The same firmware serves the iPad and the Mac; when both are connected, whichever is playing is shown. About shows the source ("Mac helper (USB)").
- Firmware test on the host: `sim/test_host_link.cpp` checks the parser against lines the helper produced. Helper tests: `mac-helper/tests/test_helper.py` (fake Mac, fake board on a pseudo-terminal).
- Adds ArduinoJson (MIT) to the firmware and bundles pyserial (BSD-3-Clause) with the helper.
- Prebuilt images: `release/media-ctrl-v0.2.0-cyd2usb.bin` (0x0) and `release/media-ctrl-v0.2.0-cyd2usb-app-0x10000.bin` (0x10000, keeps pairings).

## Docs — 2026-10-23
- Case & parts section: links to the shared [WOODWERD CYD Case](https://github.com/WOODWERD/cyd-case) with a short BOM.

## v0.1.1 — 2026-10-23
- The board now advertises as a keyboard, the appearance macOS lists, so it shows up in a Mac's Bluetooth settings. The HID map still has no keyboard collection, so the iPad keeps its on-screen keyboard.
- **Pair another device** (About): releases the current host for 60 s so a second iPad, iPhone or Mac can pair.
- The pairing screen and keys-only screen now cover the Mac as well as the iPad.
- Prebuilt images: `release/media-ctrl-v0.1.1-cyd2usb.bin` (full, address 0x0) and `release/media-ctrl-v0.1.1-cyd2usb-app-0x10000.bin` (app only, keeps pairings).

## v0.1.0 — 2026-10-23
- First build. Portrait UI with Now playing, More, Nothing playing, pairing and About screens; green accent.
- Bluetooth LE: HID consumer-control media keys, plus an Apple Media Service client for app, title, artist, progress, volume, shuffle and repeat, and remote commands (skip ±15 s, like, shuffle, repeat).
- Random static address with automatic recovery from stale bonds; a full flash clears pairings.
- Flip screen; backlight dims after 2 min; `#alive` serial heartbeat; host simulator for every screen.
