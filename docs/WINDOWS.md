# Media Ctrl on Windows

Windows doesn't share what's playing with Bluetooth accessories, so a small helper supplies it. It talks to the board over the **USB cable** and reads whatever shows in the Windows media overlay, such as Spotify and most browsers.

## What you get

| | With the helper | Without it (Bluetooth media keys only) |
|---|---|---|
| App, title, artist, progress | ✓ when the app shares them | — |
| Play/pause, previous, next | ✓ | ✓ |
| Volume slider and the volume number | ✓ exact level | steps only, no number |
| Mute | ✓ | ✓ |
| −15 s / +15 s, shuffle, repeat | ✓ when the app allows them | — |
| Like | — Windows has no such command | — |

Some apps share less than others. If an app sends no timeline, the board hides the progress bar. Tiles an app doesn't allow are dimmed.

## Install

You need the board on a USB cable to the PC, on the current firmware.

1. Put `MediaCtrlHelper.exe` and the scripts together in a folder.
2. Double-click `install.cmd`. This copies the helper to your user folder and starts it now and at each sign-in. It needs no administrator rights.
3. Windows may warn that the app is from an unknown publisher, because the helper isn't signed yet. Choose **More info**, then **Run anyway**.
4. Play something. Within a couple of seconds the board shows the track, with the device as Windows.

If no COM port appears for the board, its CH340 USB driver is missing. Windows Update usually installs it.

## Day to day

- **Flashing the board:** the helper holds the USB port. Run `stop.cmd` first, flash with ESP Ctrl, then `start.cmd`.
- **Log:** `%LOCALAPPDATA%\MediaCtrl\MediaCtrl.log` records when the board is found and lost, and each command.
- **Remove:** `uninstall.cmd`.
- **Several devices:** the board stays on the last one that was playing until another starts. Hold the top right of the screen to choose one yourself.
- **Restart on connect:** the board restarts once when the helper connects. That's normal. A port that doesn't answer is asked only once, so other boards on USB aren't disturbed.
