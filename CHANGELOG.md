# Changelog

## x0.3.0 (beta), 2026-11-02
The first public beta. Everything is over Bluetooth now. The USB cable powers the board and is used for flashing and diagnostics.

- **Three devices at once.** An iPhone, an iPad and a Mac (or any three) stay connected together, and the board remembers six. Each keeps its own track and controls.
- **Devices screen.** Hold the top right of the screen to see every connected and saved device, tap one to switch, pair another, or forget one. The board also follows whichever device starts playing, and pausing never moves it. See [Getting started](docs/GETTING_STARTED.md).
- **Forget one device.** Hold its row, then tap again. Every other device stays paired. About's button is now **Forget all devices**.
- **Top right names the device:** "Spotify iPhone", "YouTube iPad". Switching shows "Switched to …".
- **Mac over Bluetooth.** Play/pause, previous, next and volume, sent to whichever app played last. No track info yet; a Bluetooth helper is planned. See [Media Ctrl on a Mac](docs/MAC.md).
- **New look** from WOODWERD Digital: amber play button, volume slider in place of − and +, and a **More** button.
- Fixes: long-press on the title opens About again; the pairing screen shows during Pair a device; pairing a second device no longer drops the first.
- Media over USB (the v0.2.0 Mac helper) is off by default (`-DUSB_MEDIA=1` brings it back). The helpers are moving to Bluetooth. A Windows helper is in `windows-helper/` but isn't used yet.
- Prebuilt images: `release/media-ctrl-x0.3.0-cyd2usb.bin` (0x0, clean pairings) and `release/media-ctrl-x0.3.0-cyd2usb-app-0x10000.bin` (0x10000, keeps pairings).

## Docs — 2026-11-01
- The docs site moves to WOODWERD Digital through the shared theme: the WOODWERD Toolkit strip with an All tools menu,
  a product header with no brand mark, the mark in the footer only, ink primary buttons, and Inter and JetBrains Mono
  bundled with the site instead of fetched from Google Fonts. The theme control now also follows the computer.

## Docs — 2026-10-29
- The docs site now wears the shared WOODWERD docs theme ([WOODWERD/ctrl-docs-theme](https://github.com/WOODWERD/ctrl-docs-theme)):
  WOODWERD Design mark, ink header with an amber rule, warm cream pages, Inter and JetBrains Mono, and a strip
  linking the Ctrl family. Docs build pinned to MkDocs 1.6 and Material 9.7.7.

## v0.2.0 — 2026-10-23
- **Mac support with full Now Playing.** A new helper app (`mac-helper/`) reads Spotify and Apple Music on the Mac and feeds the board over the USB cable. It also carries button presses back as AppleScript: play/pause, previous/next, ±15 s, shuffle, repeat, like (Music) and system volume/mute. Install with `./install.sh` (LaunchAgent; uses the Mac's built-in Python). See [docs/MAC.md](docs/MAC.md).
- Firmware: a USB host link (JSON lines at 115200, documented in `src/host_link.h`) alongside Bluetooth. The same firmware serves the iPad and the Mac; when both are connected, whichever is playing is shown. About shows the source ("Mac helper (USB)").
- Portrait now defaults to the USB cable at the **bottom** (it was upside down). About › Flip screen still turns it round.
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
