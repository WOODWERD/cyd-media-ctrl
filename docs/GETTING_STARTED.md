# Getting started

## 1. Pair

When the board has no host, it shows **Not connected** with an amber dot and walks you through pairing. The step you're on is highlighted.

![Pairing screen](screen-pair.png)

1. On the iPad, open **Settings › Bluetooth**.
2. Tap **Media Ctrl** under Other Devices.
3. Tap **Pair** when the iPad asks.

The board remembers the iPad and reconnects on its own after power-up.

If the iPad still lists an older "Media Ctrl" that won't connect (after a full reflash, for example), tap ⓘ › **Forget This Device** and pair again.

## 2. Now playing

![Now playing](screen-now-playing.png)

| Element | What it does |
|---|---|
| Top right | The app that's playing (Spotify, Music…). A white dot means connected. |
| Title, artist | The current track. Long titles take two lines; long artist names end in "…". |
| Progress bar | Elapsed time on the left, remaining time on the right. The board counts it forward between updates from the iPad. |
| ⏮ | Previous track, or back to the start of the track (the app decides). |
| ▶ / ❚❚ (green) | Play or pause. The icon follows the iPad's real state. |
| ⏭ | Next track. |
| − / + | iPad volume down and up. The number and bar follow the iPad, including changes made on the iPad itself. |
| Middle of the volume row | Tap it to switch pages. The two dots show which page you're on. |

## 3. More (swipe left)

![More](screen-more.png)

| Tile | What it does |
|---|---|
| −15 s / +15 s | Skip within the track. The app sets the real interval, usually 15 s for podcasts and audiobooks. |
| Shuffle | Cycles the app's shuffle mode. Green means on. |
| Repeat | Cycles off, all and one. Green means on; the label shows which mode. |
| Like | Likes the current track, in apps that support it. |
| Mute | Sends the mute media key. |

Tiles the current app doesn't support are dimmed. Swipe right, or tap **‹ Now playing**, to go back.

## 4. When nothing is playing

![Nothing playing](screen-nothing-playing.png)

Play still works. It resumes whatever app last played. Previous and next are disabled until something is loaded.

## 5. Keys only

![Keys only](screen-keys-only.png)

If the host doesn't publish Now Playing information (a Mac, or an iPad that hasn't offered it yet), the board shows **Connected · keys only**. Play/pause, previous, next and volume still work as standard media keys.

## 6. About (long-press the title)

![About](screen-about.png)

- **Name / Address / Now playing:** the board's Bluetooth name, its address, and whether Now Playing data is connected.
- **Pair another device:** releases the current host for a minute so a second iPad, iPhone or Mac can pair. Afterwards, whichever device is in range reconnects.
- **Flip screen:** rotates the display 180° to suit how you stand the board. The setting is remembered.
- **Forget pairing:** tap twice. This wipes every pairing and gives the board a new Bluetooth address, so hosts see it as a new device. Also remove the old entry on each host.
- **Close.**

## Troubleshooting

| Symptom | Fix |
|---|---|
| Doesn't appear on the Mac | It's connected to the iPad. Use **Pair another device**, or turn off Bluetooth on the iPad. |
| iPad shows Media Ctrl but it won't connect | That's an old pairing. Forget it on the iPad and pair again. After four failed attempts the board also gives itself a new address. |
| Title never appears, screen says "keys only" | The iPad hasn't offered Now Playing data yet. Play something for a few seconds; the board keeps asking for about a minute after connecting. If it never appears, forget the device on the iPad and pair again. |
| Taps land in the wrong place | Use **Flip screen**. If touch is still mirrored, see [Firmware & code](firmware.md) for `-DTOUCH_DEBUG`. |
| Screen dim | It dims after two minutes. Touch it, or change track. |

The serial monitor (115200 baud) logs `[ble]`, `[ams]` and `[hid]` events, plus a `#alive` heartbeat every 5 s.
