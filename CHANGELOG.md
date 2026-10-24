# Changelog

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
