// Host-side test of the firmware's USB parser (src/host_link.cpp) fed with lines the Mac helper produced.
#include <Arduino.h>
#include <cassert>
#include <fstream>
uint32_t g_millis = 1000;
SerialStub Serial;
EspStub ESP;
#include "../src/host_link.h"

int main() {
  HostLink::begin("0.2.0");
  assert(Serial.out.find("\"dev\":\"media-ctrl\"") != std::string::npos);    // hello on boot
  assert(!HostLink::active());
  // log noise, a who, garbage, an over-long line, then the helper's real lines
  std::string feed = "#alive up=3 heap=1\r\n{\"t\":\"who\"}\n{not json\n{" + std::string(3000, 'x') + "}\n";
  std::ifstream f("../mac-helper/tests/sample_lines.jsonl");
  std::string line; while (std::getline(f, line)) feed += line + "\n";
  Serial.out.clear(); Serial.in = feed;
  HostLink::tick();
  assert(Serial.out.find("hello") != std::string::npos);                     // replied to who
  assert(HostLink::active());
  Media::NowPlaying np; HostLink::snapshot(np);
  assert(np.link == Media::LINK_HOST);
  assert(!strcmp(np.player, "Spotify") && !strcmp(np.title, "Next Song") && !strcmp(np.artist, "The Hollow Pines"));
  assert(np.duration == 253.0f && np.state == 1 && np.volume == 62 && np.shuffle == 2 && np.repeat == 0);
  assert((np.supported & (1u << Media::CMD_NEXT)) && (np.supported & (1u << 15)) && !(np.supported & (1u << Media::CMD_LIKE)));
  // UTF-8 title cut cleanly when too long
  std::string t = "{\"t\":\"np\",\"app\":\"Music\",\"title\":\"";
  for (int i = 0; i < 100; i++) t += "é";
  t += "\",\"artist\":\"Señor Coconut\",\"state\":0,\"sup\":[11]}\n";
  Serial.in = t; Serial.pos = 0; HostLink::tick(); HostLink::snapshot(np);
  assert(strlen(np.title) < sizeof np.title && (strlen(np.title) % 2) == 0 && !strcmp(np.artist, "Señor Coconut"));
  assert(np.state == 0 && (np.supported & (1u << Media::CMD_LIKE)));
  // idle keeps volume, clears the track
  Serial.in = "{\"t\":\"idle\",\"vol\":40}\n"; Serial.pos = 0; HostLink::tick(); HostLink::snapshot(np);
  assert(np.title[0] == 0 && np.volume == 40 && np.state == -1 && (np.supported & (1u << Media::CMD_TOGGLE)));
  // a button press goes out as one JSON line
  Serial.out.clear(); HostLink::send(Media::CMD_SKIP_FWD);
  assert(Serial.out == "{\"t\":\"cmd\",\"c\":9}\n");
  // helper goes quiet → link drops after the timeout
  g_millis += 9000; assert(!HostLink::active());
  puts("\nhost_link: all checks passed");
}
