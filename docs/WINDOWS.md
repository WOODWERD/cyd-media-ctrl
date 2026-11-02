# Media Ctrl on Windows

A Windows PC pairs with Media Ctrl over Bluetooth: **Settings › Bluetooth & devices › Add device › Bluetooth**, then choose **Media Ctrl**. Windows treats it as a keyboard-type accessory, which is expected.

## What you get

Windows doesn't share what's playing with Bluetooth accessories, so for now the board controls the PC with media keys, the same as a Mac (see [Media Ctrl on a Mac](MAC.md)):

| | On Windows today |
|---|---|
| Play/pause, previous, next | ✓, aimed at whatever Windows shows in its media overlay |
| Volume, mute | ✓ in steps |
| App, title, artist, progress, More tiles | Not yet |

Windows on Media Ctrl hasn't been tested on hardware yet.

## Coming: track info

A small helper app will read the Windows media overlay, which covers Spotify and most browsers, and send it to the board over Bluetooth. The board will then show the track and the More tiles will work. A first version of the helper, which used the USB cable, is in `windows-helper/`. It's being moved to Bluetooth before release, so the cable only powers and flashes the board.
