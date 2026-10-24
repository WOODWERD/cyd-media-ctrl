// Media Ctrl USB host link: the Mac helper (mac-helper/) talks to the board over the same USB
// serial port used for flashing, one JSON object per line, 115200 baud.
//
//   helper → board   {"t":"who"}                                   are you a Media Ctrl?
//                    {"t":"host","name":"Mac"}                     who I am (shown top right when idle)
//                    {"t":"np","app":"Spotify","title":"…","artist":"…","dur":253.1,"pos":102.4,
//                     "state":1,"vol":62,"muted":0,"shuffle":1,"repeat":0,"sup":[0,1,2,3,…]}
//                    {"t":"idle","vol":62}                         nothing playing
//   board → helper   {"t":"hello","dev":"media-ctrl","fw":"0.2.0"} on boot and in reply to who
//                    {"t":"cmd","c":2}                             a button (Media::Cmd numbering)
//
// Lines that don't start with '{' (the firmware's own log) are ignored by both sides.
// The helper counts as present for HOST_TIMEOUT_MS after its last line.
#pragma once
#include "ble_media.h"

namespace HostLink {
void begin(const char* fwVersion);
void tick();                              // read and parse serial input; call from loop()
bool active();                            // a helper line arrived recently
void snapshot(Media::NowPlaying& out);    // helper's now-playing, link = LINK_HOST
void send(Media::Cmd c);
// parse one line (exposed for the host-side test)
void handleLine(const char* line);
}  // namespace HostLink
