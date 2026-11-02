// Media Ctrl Bluetooth layer.
// One BLE link to the iPad carries two things:
//   1. A HID "consumer control" device (the media keys every keyboard has): always works, blind.
//   2. An Apple Media Service (AMS) client: the iPad publishes what is playing (app, title, artist,
//      duration, elapsed, volume, shuffle/repeat) and accepts remote commands (play, skip ±, like…).
// When AMS is available it is used for everything it can do; the HID keys are the fallback.
#pragma once
#include <Arduino.h>

namespace Media {

// AMS RemoteCommandID values (Apple Media Service spec)
enum Cmd : uint8_t {
  CMD_PLAY = 0, CMD_PAUSE = 1, CMD_TOGGLE = 2, CMD_NEXT = 3, CMD_PREV = 4,
  CMD_VOL_UP = 5, CMD_VOL_DOWN = 6, CMD_REPEAT = 7, CMD_SHUFFLE = 8,
  CMD_SKIP_FWD = 9, CMD_SKIP_BACK = 10, CMD_LIKE = 11,
  CMD_MUTE = 0xF0   // HID only (AMS has no mute)
};

enum Link : uint8_t {
  LINK_ADVERTISING,   // nobody connected
  LINK_PAIRING,       // connected, waiting for the iPad to finish pairing/encryption
  LINK_READY,         // encrypted; looking for AMS
  LINK_AMS,           // now-playing info flowing
  LINK_BLIND,         // connected, but the iPad offers no AMS - media keys only
  LINK_HOST           // the Mac helper is feeding now-playing over USB (see host_link.h)
};

struct NowPlaying {
  Link link = LINK_ADVERTISING;
  char player[40] = "";     // app name, e.g. "Spotify"
  char title[160] = "";
  char artist[120] = "";
  float duration = 0;       // seconds, 0 = unknown
  float elapsed = 0;        // seconds at elapsedAt
  float rate = 0;           // playback rate at elapsedAt (0 = paused)
  uint32_t elapsedAt = 0;   // millis() when elapsed was reported
  int8_t state = -1;        // -1 unknown, 0 paused, 1 playing, 2 rewinding, 3 fast-forwarding
  int8_t volume = -1;       // 0-100, -1 unknown
  int8_t shuffle = -1;      // 0 off, 1 one, 2 all
  int8_t repeat = -1;       // 0 off, 1 one, 2 all
  uint16_t supported = 0;   // bit n set = AMS command n currently supported by the app
  uint32_t rev = 0;         // bumps on every change
};

void begin(const char* deviceName);
void tick();                         // call from loop()
void snapshot(NowPlaying& out);      // thread-safe copy for the UI
void send(Cmd c);                    // AMS when available, else the matching HID media key
bool supports(const NowPlaying& np, Cmd c);
void pairAnother();                  // open a 60 s pair window; drops a host only with every slot full
bool pairWindow();
struct Device {                      // one host for the Devices screen (MC-S11)
  int id;                            // a Bluetooth slot from 0, the helper after them, -1 saved and not connected
  char name[32], sub[48];            // "My iPhone", "iPhone · Bluetooth · Spotify"
  char kind[8];                      // "iPhone", "iPad" or ""
  int8_t state;                      // as NowPlaying.state
  bool shown, saved, helper;
  uint8_t addr[6], addrType;         // Bluetooth identity, for forget()
};
int devices(Device* out, int cap);   // connected hosts, then saved ones (MC-R58); returns the count
bool forget(const Device& d);        // forget one Bluetooth device, keeping every other bond (MC-R53)
void showSource(int id);             // show this host now; it stands until another starts (MC-R47)
void setVolume(int level);           // exact level, helper hosts only (MC-R48)
const char* shownName();             // the shown source's own name, or "Bluetooth"
const char* shownKind();             // "iPhone", "iPad", "Mac", "Windows", or "" if unknown
void forgetPairing();                // wipe bonds, new Bluetooth address, reboot
String address();

}  // namespace Media
