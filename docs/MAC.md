# Media Ctrl on a Mac

A Mac pairs with Media Ctrl over Bluetooth, the same way an iPhone or iPad does. No app or cable is needed.

## Pair

1. Open **System Settings › Bluetooth**.
2. Find **Media Ctrl** among nearby devices and click **Connect**. macOS lists it as a keyboard-type accessory. That's expected, and it doesn't take over your keyboard.
3. Play something. On the board, hold the top right of the screen to open **Devices** and tap your Mac, or just start playing on the Mac when nothing else is.

If Media Ctrl doesn't appear, three devices are already connected. On the board, open Devices and tap **Pair a device**, then look again.

## What you get

macOS doesn't share what's playing with Bluetooth accessories, so the board can control the Mac but can't show the track.

| | On a Mac today |
|---|---|
| Play/pause | ✓. The button shows play and pause together, because the Mac doesn't say which state it's in. |
| Previous, next | ✓ in apps with a queue, such as Spotify and Music. YouTube skips only within a playlist. |
| Volume | ✓ in steps |
| Mute | ✓ |
| App, title, artist, progress | Not yet |
| −15 s / +15 s, shuffle, repeat, like | Not yet |

**The buttons go to the app that played last.** This is how macOS treats media keys from any keyboard or headset. Start a video in the browser and the board controls it; start Spotify and it moves back to Spotify.

## Coming: track info

A small helper app on the Mac will read what's playing and send it to the board over Bluetooth, so the board shows the track and the More tiles work, with no cable. Until then the board shows a plain "buttons only" screen for the Mac.

An earlier version of the helper sent this over the USB cable. That route is switched off in the firmware, so the board can run on battery later and the cable only powers and flashes it. The code is still in `mac-helper/` and will move to Bluetooth.
