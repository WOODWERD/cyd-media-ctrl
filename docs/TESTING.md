# Bench test

Run this checklist on real hardware before every release. Note the firmware version, the host and the app.

| # | Step | Pass when |
|---|---|---|
| 1 | Full flash (`0x0`). | Boots to **Pair a device**. LED blinks blue. |
| 2 | Pair from the iPad (Settings › Bluetooth). | Step 3 highlights, then the screen changes to Now playing. Dot turns white. |
| 3 | Start Spotify. | App name, title, artist appear within ~2 s. Progress runs. |
| 4 | Play/pause from the board. | iPad pauses and resumes. Icon follows. |
| 5 | Next, then previous. | Track changes; title updates. |
| 6 | Volume − and +, then change volume on the iPad. | The number follows both. |
| 7 | Swipe left: −15 s / +15 s. | Position jumps (or the tile is dimmed if the app doesn't support it). |
| 8 | Shuffle, repeat. | State on the iPad changes; tiles turn green; repeat label shows the mode. |
| 9 | Like, mute. | Toast shows. Mute toggles iPad audio. |
| 10 | Long title / non-ASCII artist. | Title on two lines with "…"; accents render. |
| 11 | Close the app. | **Nothing playing**; play resumes the last app. |
| 12 | Leave out of range, return. | Reconnects without pairing again. |
| 13 | iPad on-screen keyboard while connected. | Still appears (the board is not a keyboard). |
| 14 | Pair a Mac from System Settings › Bluetooth, with the iPad still connected. | Mac connects; the iPad stays connected. Choose the Mac on Devices: **Connected**, "Mac" top right, play and pause shown together; play/pause, next, previous and volume work in Spotify. |
| 15 | Disconnect on the Mac. | The board moves to the iPad; the iPad never dropped. |
| 16 | About › Flip screen. | Display and touch both rotate correctly. |
| 17 | About › Forget all devices (two taps). | Reboots to the pairing screen with a new address. |
| 18 | Untouched for 2 min. | Backlight dims; a touch or a new track wakes it. |
| 19 | Serial at 115200. | `#alive` every 5 s; no resets. |
Steps 20 to 26 need a build with `-DUSB_MEDIA=1` and the USB helper. Skip them on the default build.

| # | Step | Pass when |
|---|---|---|
| 20 | Mac: `mac-helper/install.sh`, board on USB. | Log shows "found Media Ctrl"; board was **not** reset when the helper opened the port. |
| 21 | Mac: play in Spotify. | App, title, artist, progress and system volume appear; Like tile dimmed. |
| 22 | Mac: every button (transport, ±15 s, shuffle, repeat, volume, mute). | Spotify / system responds; screen updates within ~1 s. |
| 23 | Mac: switch to Music, Like. | Track is favorited in Music. |
| 24 | Mac: quit the apps. | **Nothing playing** with "Mac" top right; play resumes the last app. |
| 25 | iPad playing over Bluetooth while the Mac helper is idle. | iPad's track shows; pause it and the board stays on the iPad, and play resumes it. Start Spotify on the Mac and the Mac's track takes over. |
| 26 | `./stop.sh`, flash with ESP Ctrl, `./start.sh`. | Port free for flashing; helper reconnects afterwards. |

## Several devices

| # | Step | Pass when |
|---|---|---|
| 27 | iPhone, iPad and Mac paired. Play on each in turn. | The board follows whichever started last, with its track (or the keys-only screen for the Mac). |
| 28 | Pause the device on screen. | The board stays on it; play resumes it. |
| 29 | Hold the top right. | **Devices** lists all three with what each is playing; the one on screen has an amber outline. Tap another: "Switched to …", and only that device responds to the buttons. |
| 30 | Turn off the iPad's Bluetooth, open Devices. | The iPad is under **Saved** as "Not connected", by name. |
| 31 | Hold the iPhone's row, then tap it again. | "Forgot …" and the note to forget it on the iPhone; the iPad and Mac stay connected. |
| 32 | Hold a row, then wait 4 s. | The row goes back; nothing is forgotten. |
| 33 | Restart the board. | Saved names are still listed. |
