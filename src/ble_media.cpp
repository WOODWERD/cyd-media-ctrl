#include "ble_media.h"
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <Preferences.h>
#include "host_link.h"

// ---------------------------------------------------------------- HID: consumer control only
// One 16-bit usage per report (array style). No keyboard collection, so the iPad keeps its
// on-screen keyboard - a "keyboard" device would hide it.
static const uint8_t REPORT_MAP[] = {
  0x05, 0x0C,        // Usage Page (Consumer)
  0x09, 0x01,        // Usage (Consumer Control)
  0xA1, 0x01,        // Collection (Application)
  0x85, 0x01,        //   Report ID (1)
  0x15, 0x00,        //   Logical Minimum (0)
  0x26, 0xFF, 0x03,  //   Logical Maximum (0x3FF)
  0x19, 0x00,        //   Usage Minimum (0)
  0x2A, 0xFF, 0x03,  //   Usage Maximum (0x3FF)
  0x75, 0x10,        //   Report Size (16)
  0x95, 0x01,        //   Report Count (1)
  0x81, 0x00,        //   Input (Data, Array, Absolute)
  0xC0               // End Collection
};
// Consumer page usages
static const uint16_t U_PLAY = 0xB0, U_PAUSE = 0xB1, U_NEXT = 0xB5, U_PREV = 0xB6, U_FF = 0xB3, U_REW = 0xB4,
                      U_PLAYPAUSE = 0xCD, U_MUTE = 0xE2, U_VOL_UP = 0xE9, U_VOL_DOWN = 0xEA;

// ---------------------------------------------------------------- AMS UUIDs
static const char* AMS_SVC    = "89D3502B-0F36-433A-8EF4-C502AD55F8DC";
static const char* AMS_REMOTE = "9B3C81D8-57B1-4A8A-B8DF-0E56F7CA51C2";
static const char* AMS_UPDATE = "2F7CABCE-808D-411F-9A0C-BB92BA96C102";
static const char* AMS_ATTR   = "C6B2F38C-23AB-46D8-A6AB-A3A870BBD5D7";
// AMS service UUID, little-endian, for the "solicited service" advert field
static const uint8_t AMS_UUID_LE[16] = {0xDC, 0xF8, 0x55, 0xAD, 0x02, 0xC5, 0xF4, 0x8E,
                                        0x3A, 0x43, 0x36, 0x0F, 0x2B, 0x50, 0xD3, 0x89};
enum { ENT_PLAYER = 0, ENT_QUEUE = 1, ENT_TRACK = 2 };
enum { PL_NAME = 0, PL_INFO = 1, PL_VOLUME = 2 };
enum { Q_INDEX = 0, Q_COUNT = 1, Q_SHUFFLE = 2, Q_REPEAT = 3 };
enum { TR_ARTIST = 0, TR_ALBUM = 1, TR_TITLE = 2, TR_DURATION = 3 };

// ---------------------------------------------------------------- state
static const int SLOTS = 2;                  // Bluetooth hosts held at once (MC-R39)
static const int SRC_HOST = SLOTS;           // the USB helper, as a source after the slots

struct Slot {
  volatile uint16_t conn = 0xFFFF;
  Media::NowPlaying np;                      // guarded by mux
  NimBLERemoteCharacteristic *chRemote = nullptr, *chUpdate = nullptr, *chAttr = nullptr;
  volatile uint32_t amsAt = 0;               // when to (re)try AMS discovery, 0 = not scheduled
  uint8_t amsTries = 0;
  volatile bool needFullTitle = false, needFullArtist = false;
  uint32_t connectedAt = 0;
  char name[24] = "";                        // the host's own name, read after AMS is up
  char kind[8] = "";                         // "iPhone" or "iPad", from its model string
};

static NimBLEServer* srv = nullptr;
static NimBLEHIDDevice* hid = nullptr;
static NimBLECharacteristic* input = nullptr;

static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static Slot slots[SLOTS];
static int shown = SRC_HOST;                 // the source on screen
static bool played = false;                  // something has played since boot, or a source was chosen
static uint8_t quickDrops = 0;               // host killed the link before encryption, in a row
static Preferences prefs;
static uint8_t ownAddr[6];
// "Pair another device" with both slots full: drop one host and refuse it for a minute
static volatile uint32_t pairWindowUntil = 0;
static NimBLEAddress kickedPeer;
static bool inPairWindow() { return pairWindowUntil && (int32_t)(millis() - pairWindowUntil) < 0; }
static bool isKicked(const NimBLEConnInfo& info) {
  return inPairWindow() && !kickedPeer.isNull() && info.getIdAddress() == kickedPeer;
}

static Slot* byConn(uint16_t conn) {
  for (Slot& s : slots) if (s.conn == conn) return &s;
  return nullptr;
}
static Slot* freeSlot() { return byConn(0xFFFF); }

static void bump(Slot& s) { s.np.rev++; }
static void setLink(Slot& s, Media::Link l) { portENTER_CRITICAL(&mux); s.np.link = l; bump(s); portEXIT_CRITICAL(&mux); }

static void clearTrack(Media::NowPlaying& np) {
  np.player[0] = np.title[0] = np.artist[0] = 0;
  np.duration = np.elapsed = np.rate = 0; np.elapsedAt = millis();
  np.state = -1; np.volume = -1; np.shuffle = -1; np.repeat = -1; np.supported = 0;
}

static void copyStr(char* dst, size_t cap, const uint8_t* src, size_t n) {
  if (n >= cap) n = cap - 1;
  memcpy(dst, src, n); dst[n] = 0;
  // never cut a UTF-8 sequence in half
  size_t i = n;
  while (i > 0 && (dst[i - 1] & 0xC0) == 0x80) i--;
  if (i > 0 && (dst[i - 1] & 0x80)) {
    uint8_t lead = dst[i - 1]; size_t need = (lead & 0xE0) == 0xC0 ? 2 : (lead & 0xF0) == 0xE0 ? 3 : 4;
    if (n - (i - 1) < need) dst[i - 1] = 0;
  }
}

// ---------------------------------------------------------------- AMS notifications (NimBLE task)
static void entityUpdate(Slot& s, uint8_t* d, size_t len) {
  if (len < 3) return;
  Media::NowPlaying& np = s.np;
  const uint8_t ent = d[0], attr = d[1], flags = d[2];
  const uint8_t* v = d + 3; const size_t n = len - 3;
  char tmp[64];
  portENTER_CRITICAL(&mux);
  if (ent == ENT_PLAYER && attr == PL_NAME) {
    copyStr(np.player, sizeof np.player, v, n);
    if (!np.player[0]) {   // the media app went away
      np.title[0] = np.artist[0] = 0; np.duration = 0; np.state = -1;
    }
  } else if (ent == ENT_PLAYER && attr == PL_INFO) {
    // "PlaybackState,PlaybackRate,ElapsedTime"
    copyStr(tmp, sizeof tmp, v, n);
    int st = -1; float rate = 0, el = 0;
    char* p = tmp; st = atoi(p);
    if ((p = strchr(p, ','))) { rate = atof(++p); if ((p = strchr(p, ','))) el = atof(++p); }
    np.state = n ? st : -1; np.rate = rate; np.elapsed = el; np.elapsedAt = millis();
  } else if (ent == ENT_PLAYER && attr == PL_VOLUME) {
    copyStr(tmp, sizeof tmp, v, n);
    np.volume = n ? (int8_t)lroundf(atof(tmp) * 100.0f) : -1;
  } else if (ent == ENT_QUEUE && attr == Q_SHUFFLE) {
    copyStr(tmp, sizeof tmp, v, n); np.shuffle = n ? atoi(tmp) : -1;
  } else if (ent == ENT_QUEUE && attr == Q_REPEAT) {
    copyStr(tmp, sizeof tmp, v, n); np.repeat = n ? atoi(tmp) : -1;
  } else if (ent == ENT_TRACK && attr == TR_TITLE) {
    copyStr(np.title, sizeof np.title, v, n); if (flags & 1) s.needFullTitle = true;
  } else if (ent == ENT_TRACK && attr == TR_ARTIST) {
    copyStr(np.artist, sizeof np.artist, v, n); if (flags & 1) s.needFullArtist = true;
  } else if (ent == ENT_TRACK && attr == TR_DURATION) {
    copyStr(tmp, sizeof tmp, v, n); np.duration = n ? atof(tmp) : 0;
  }
  bump(s);
  portEXIT_CRITICAL(&mux);
}

static void remoteCommands(Slot& s, uint8_t* d, size_t len) {
  uint16_t mask = 0;
  for (size_t i = 0; i < len; i++) if (d[i] < 16) mask |= 1u << d[i];
  portENTER_CRITICAL(&mux); s.np.supported = mask; bump(s); portEXIT_CRITICAL(&mux);
  Serial.printf("[ams] supported commands 0x%04X\n", mask);
}

// NimBLE's callbacks carry no user data, so each slot gets its own pair
template <int I> static void onEntityUpdate(NimBLERemoteCharacteristic*, uint8_t* d, size_t len, bool) { entityUpdate(slots[I], d, len); }
template <int I> static void onRemoteCommands(NimBLERemoteCharacteristic*, uint8_t* d, size_t len, bool) { remoteCommands(slots[I], d, len); }

// ---------------------------------------------------------------- server callbacks
class ServerCB : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer*, NimBLEConnInfo& info) override {
    Slot* s = freeSlot();
    if (!s) { srv->disconnect(info); return; }
    s->conn = info.getConnHandle();
    s->connectedAt = millis();
    s->chRemote = s->chUpdate = s->chAttr = nullptr;
    s->amsAt = 0; s->amsTries = 0; s->name[0] = s->kind[0] = 0;
    portENTER_CRITICAL(&mux); clearTrack(s->np); s->np.link = Media::LINK_PAIRING; bump(*s); portEXIT_CRITICAL(&mux);
    Serial.printf("[ble] connected %s (id %s)\n", info.getAddress().toString().c_str(), info.getIdAddress().toString().c_str());
    if (isKicked(info)) { Serial.println("[ble] previous host - refused during pair window"); srv->disconnect(info); return; }
    if (freeSlot()) NimBLEDevice::getAdvertising()->start();   // leave room for the next host (MC-R41)
    // iOS pairs on its own when it reads the HID map; this just nudges a bonded peer to re-encrypt fast
    NimBLEDevice::startSecurity(info.getConnHandle());
  }
  void onDisconnect(NimBLEServer*, NimBLEConnInfo& info, int reason) override {
    Slot* s = byConn(info.getConnHandle());
    if (!s) return;
    const uint32_t up = millis() - s->connectedAt;
    const bool quick = up < 1500 && s->np.link == Media::LINK_PAIRING &&
                       (reason == 0x213 || reason == 0x205 || reason == 0x206 || reason == 0x23D);
    quickDrops = quick ? quickDrops + 1 : 0;
    s->conn = 0xFFFF; s->amsAt = 0;
    s->chRemote = s->chUpdate = s->chAttr = nullptr;
    s->name[0] = s->kind[0] = 0;
    portENTER_CRITICAL(&mux); clearTrack(s->np); s->np.link = Media::LINK_ADVERTISING; bump(*s); portEXIT_CRITICAL(&mux);
    Serial.printf("[ble] disconnected 0x%03X after %lu ms%s\n", reason, (unsigned long)up, quick ? " (pairing refused)" : "");
  }
  void onAuthenticationComplete(NimBLEConnInfo& info) override {
    Serial.printf("[ble] auth complete bonded=%d encrypted=%d\n", info.isBonded(), info.isEncrypted());
    if (!info.isEncrypted()) return;
    if (isKicked(info)) { Serial.println("[ble] previous host - refused during pair window"); srv->disconnect(info); return; }
    Slot* s = byConn(info.getConnHandle());
    if (!s) return;
    if (inPairWindow()) pairWindowUntil = 0;   // a device paired: window done
    quickDrops = 0;
    setLink(*s, Media::LINK_READY);
    s->amsAt = millis() + 1200;   // let iOS finish its own HID discovery first
  }
  void onMTUChange(uint16_t mtu, NimBLEConnInfo&) override { Serial.printf("[ble] MTU %u\n", mtu); }
};

// ---------------------------------------------------------------- AMS setup (main loop)
static bool registerAttrs(Slot& s, uint8_t ent, std::initializer_list<uint8_t> attrs) {
  std::vector<uint8_t> v{ent};
  v.insert(v.end(), attrs.begin(), attrs.end());
  return s.chUpdate->writeValue(v.data(), v.size(), true);
}

static void readIdentity(Slot& s, NimBLEClient* cl) {
  NimBLEAttValue n = cl->getValue(NimBLEUUID((uint16_t)0x1800), NimBLEUUID((uint16_t)0x2A00));   // GAP device name
  copyStr(s.name, sizeof s.name, n.data(), n.size());
  char model[24] = "";
  NimBLEAttValue m = cl->getValue(NimBLEUUID((uint16_t)0x180A), NimBLEUUID((uint16_t)0x2A24));   // model, "iPad14,8"
  copyStr(model, sizeof model, m.data(), m.size());
  const char* kind = !strncmp(model, "iPhone", 6) || strstr(s.name, "iPhone") ? "iPhone"
                   : !strncmp(model, "iPad", 4) || strstr(s.name, "iPad") ? "iPad" : "";
  strlcpy(s.kind, kind, sizeof s.kind);
  Serial.printf("[ble] host name \"%s\" model \"%s\"\n", s.name, model);
}

static void amsSetup(int i) {
  Slot& s = slots[i];
  s.amsAt = 0;
  if (s.conn == 0xFFFF) return;
  NimBLEClient* cl = srv->getClient(s.conn);
  NimBLERemoteService* svc = cl ? cl->getService(AMS_SVC) : nullptr;
  if (!svc) {
    // AMS is not always published (e.g. right after boot of the iPad); keep trying for a while
    s.amsTries++;
    Serial.printf("[ams] service not found (try %u)\n", s.amsTries);
    setLink(s, Media::LINK_BLIND);
    if (s.amsTries < 12) s.amsAt = millis() + 5000;
    return;
  }
  s.chRemote = svc->getCharacteristic(AMS_REMOTE);
  s.chUpdate = svc->getCharacteristic(AMS_UPDATE);
  s.chAttr   = svc->getCharacteristic(AMS_ATTR);
  if (!s.chRemote || !s.chUpdate) { Serial.println("[ams] characteristics missing"); setLink(s, Media::LINK_BLIND); return; }
  bool ok = s.chRemote->subscribe(true, i == 0 ? onRemoteCommands<0> : onRemoteCommands<1>) &&
            s.chUpdate->subscribe(true, i == 0 ? onEntityUpdate<0> : onEntityUpdate<1>);
  ok = ok && registerAttrs(s, ENT_PLAYER, {PL_NAME, PL_INFO, PL_VOLUME});
  ok = ok && registerAttrs(s, ENT_TRACK, {TR_ARTIST, TR_TITLE, TR_DURATION});
  ok = ok && registerAttrs(s, ENT_QUEUE, {Q_SHUFFLE, Q_REPEAT});
  Serial.printf("[ams] %s (MTU %u)\n", ok ? "subscribed" : "setup failed", cl->getMTU());
  if (ok) readIdentity(s, cl);
  if (ok) setLink(s, Media::LINK_AMS);
  else { setLink(s, Media::LINK_BLIND); if (++s.amsTries < 12) s.amsAt = millis() + 5000; }
}

static void readFull(Slot& s, uint8_t attr, char* dst, size_t cap) {
  if (!s.chAttr) return;
  uint8_t req[2] = {ENT_TRACK, attr};
  if (!s.chAttr->writeValue(req, 2, true)) return;
  NimBLEAttValue v = s.chAttr->readValue();
  portENTER_CRITICAL(&mux); copyStr(dst, cap, v.data(), v.size()); bump(s); portEXIT_CRITICAL(&mux);
}

// ---------------------------------------------------------------- address / bonds
static void newAddress() {
  esp_fill_random(ownAddr, 6);
  ownAddr[5] |= 0xC0;   // random static: two top bits set (little-endian, byte 5 is the MSB)
  prefs.putBytes("addr", ownAddr, 6);
}

void Media::begin(const char* name) {
  prefs.begin("mediactrl", false);
  if (prefs.getBytes("addr", ownAddr, 6) != 6) newAddress();

  NimBLEDevice::init(name);
  NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM);
  NimBLEDevice::setOwnAddr(ownAddr);
  NimBLEDevice::setMTU(247);
  NimBLEDevice::setSecurityAuth(true, false, true);    // bond, no MITM, secure connections
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

  srv = NimBLEDevice::createServer();
  srv->setCallbacks(new ServerCB());
  srv->advertiseOnDisconnect(true);

  hid = new NimBLEHIDDevice(srv);
  hid->setManufacturer("WOODWERD");
  hid->setPnp(0x02, 0xE502, 0xA112, 0x0100);
  hid->setHidInfo(0x00, 0x01);
  hid->setReportMap((uint8_t*)REPORT_MAP, sizeof(REPORT_MAP));
  input = hid->getInputReport(1);
  hid->setBatteryLevel(100);
  hid->startServices();

  NimBLEAdvertisementData ad;
  ad.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
  ad.setAppearance(0x03C1);                 // keyboard: macOS only lists HID devices it recognises. The iPad
                                            // keeps its on-screen keyboard (no keyboard collection in the map)
  ad.addServiceUUID(hid->getHidService()->getUUID());
  uint8_t sol[18] = {17, 0x15};             // "List of 128-bit Service Solicitation UUIDs": ask iOS for AMS
  memcpy(sol + 2, AMS_UUID_LE, 16);
  ad.addData(sol, sizeof sol);
  NimBLEAdvertisementData sr;
  sr.setName(name);
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->setAdvertisementData(ad);
  adv->setScanResponseData(sr);
  adv->enableScanResponse(true);
  adv->start();
  Serial.printf("[ble] advertising as \"%s\" %s\n", name, address().c_str());
}

void Media::tick() {
  HostLink::tick();
  if (quickDrops >= 4) {   // a host keeps an old bond we no longer have: become a new device
    quickDrops = 0;
    bool working = false;                   // but never at the cost of a host that's fine (MC-R46)
    for (Slot& s : slots) if (s.conn != 0xFFFF) working = true;
    if (!working) {
      Serial.println("[ble] pairing refused 4 times - new address");
      forgetPairing();
    }
  }
  for (int i = 0; i < SLOTS; i++) {
    Slot& s = slots[i];
    if (s.amsAt && (int32_t)(millis() - s.amsAt) >= 0) amsSetup(i);
    if (s.needFullTitle)  { s.needFullTitle = false;  readFull(s, TR_TITLE, s.np.title, sizeof s.np.title); }
    if (s.needFullArtist) { s.needFullArtist = false; readFull(s, TR_ARTIST, s.np.artist, sizeof s.np.artist); }
  }
}

// Up to three sources can be live at once: two Bluetooth hosts and the helper over USB.
// The board stays on the last one that was playing until a different one starts; pausing never
// moves it. Until any has played, the helper is shown (the board is on its cable). MC-R14a, MC-R42.
static bool available(int src) {
  if (src == SRC_HOST) return HostLink::active();
  portENTER_CRITICAL(&mux); const bool ams = slots[src].np.link == Media::LINK_AMS; portEXIT_CRITICAL(&mux);
  return ams;
}

static int pickSource() {
  static bool was[SLOTS + 1];
  bool playing[SLOTS + 1];
  portENTER_CRITICAL(&mux);
  for (int i = 0; i < SLOTS; i++) playing[i] = slots[i].np.link == Media::LINK_AMS && slots[i].np.state == 1;
  portEXIT_CRITICAL(&mux);
  const bool hostOn = HostLink::active();
  playing[SRC_HOST] = false;
  if (hostOn) { Media::NowPlaying h; HostLink::snapshot(h); playing[SRC_HOST] = h.state == 1; }

  for (int i = 0; i <= SLOTS; i++) {
    if (playing[i]) played = true;
    if (playing[i] && !was[i]) shown = i;
    was[i] = playing[i];
  }
  if (!played && hostOn) shown = SRC_HOST;
  if (available(shown)) return shown;

  // the shown source went away: something playing, then the helper, then any Bluetooth host
  for (int i = 0; i <= SLOTS; i++) if (playing[i]) return shown = i;
  if (hostOn) return shown = SRC_HOST;
  for (int i = 0; i < SLOTS; i++) if (available(i)) return shown = i;
  for (int i = 0; i < SLOTS; i++) if (slots[i].conn != 0xFFFF) return shown = i;
  return 0;
}

int Media::devices(Device* out, int cap) {
  const int on = pickSource();
  int n = 0;
  for (int i = 0; i < SLOTS && n < cap; i++) {
    Slot& s = slots[i];
    if (s.conn == 0xFFFF) continue;
    Device& d = out[n++];
    d.id = i; d.shown = i == on;
    strlcpy(d.name, s.name[0] ? s.name : "Bluetooth", sizeof d.name);
    portENTER_CRITICAL(&mux); d.state = s.np.state; char app[24]; strlcpy(app, s.np.player, sizeof app); portEXIT_CRITICAL(&mux);
    snprintf(d.sub, sizeof d.sub, "%s%sBluetooth%s%s", s.kind, s.kind[0] ? " · " : "", app[0] ? " · " : "", app);
  }
  if (HostLink::active() && n < cap) {
    NowPlaying h; HostLink::snapshot(h);
    Device& d = out[n++];
    d.id = SRC_HOST; d.shown = on == SRC_HOST; d.state = h.state;
    strlcpy(d.name, HostLink::osName(), sizeof d.name);
    snprintf(d.sub, sizeof d.sub, "Helper · USB%s%s", h.player[0] ? " · " : "", h.player);
  }
  return n;
}

void Media::showSource(int id) { if (id >= 0 && id <= SRC_HOST) { shown = id; played = true; } }

void Media::setVolume(int level) { HostLink::sendVolume(level); }

const char* Media::shownName() {
  if (shown == SRC_HOST) return HostLink::osName();
  return slots[shown].name[0] ? slots[shown].name : "Bluetooth";
}
const char* Media::shownKind() { return shown == SRC_HOST ? HostLink::osName() : slots[shown].kind; }

void Media::snapshot(NowPlaying& out) {
  const int src = pickSource();
  if (src == SRC_HOST) { HostLink::snapshot(out); return; }
  portENTER_CRITICAL(&mux); out = slots[src].np; portEXIT_CRITICAL(&mux);
}

bool Media::supports(const NowPlaying& s, Cmd c) {
  if (s.link == LINK_HOST) return c == CMD_MUTE ? (s.supported & (1u << 15)) : (c < 15 && (s.supported & (1u << c)));
  if (c == CMD_MUTE) return s.link >= LINK_READY;
  if (s.link == LINK_AMS) return c < 16 && (s.supported & (1u << c));
  if (s.link == LINK_READY || s.link == LINK_BLIND)   // HID keys only
    return c == CMD_TOGGLE || c == CMD_PLAY || c == CMD_PAUSE || c == CMD_NEXT || c == CMD_PREV ||
           c == CMD_VOL_UP || c == CMD_VOL_DOWN;
  return false;
}

static void hidTap(uint16_t conn, uint16_t usage) {
  if (!input || conn == 0xFFFF) return;
  uint8_t r[2] = {(uint8_t)usage, (uint8_t)(usage >> 8)};
  input->setValue(r, 2); input->notify(conn);   // the shown host only (MC-R43)
  delay(30);
  r[0] = r[1] = 0;
  input->setValue(r, 2); input->notify(conn);
}

void Media::send(Cmd c) {
  NowPlaying s; snapshot(s);
  if (s.link == LINK_HOST) { HostLink::send(c); Serial.printf("[usb] command %u\n", c); return; }
  Slot& slot = slots[shown == SRC_HOST ? 0 : shown];
  if (c != CMD_MUTE && s.link == LINK_AMS && slot.chRemote && (s.supported & (1u << c))) {
    uint8_t id = c;
    if (slot.chRemote->writeValue(&id, 1, true)) { Serial.printf("[ams] command %u\n", c); return; }
  }
  uint16_t u = 0;
  switch (c) {
    case CMD_PLAY: u = U_PLAY; break;            case CMD_PAUSE: u = U_PAUSE; break;
    case CMD_TOGGLE: u = U_PLAYPAUSE; break;     case CMD_NEXT: u = U_NEXT; break;
    case CMD_PREV: u = U_PREV; break;            case CMD_VOL_UP: u = U_VOL_UP; break;
    case CMD_VOL_DOWN: u = U_VOL_DOWN; break;    case CMD_MUTE: u = U_MUTE; break;
    default: break;   // skip ±, like, shuffle, repeat have no reliable HID key
  }
  if (u) { hidTap(slot.conn, u); Serial.printf("[hid] usage 0x%02X\n", u); }
}

void Media::pairAnother() {   // with a slot free nothing is dropped (MC-R45)
  kickedPeer = NimBLEAddress();
  pairWindowUntil = millis() + 60000;
  if (freeSlot()) { Serial.println("[ble] pair window open 60 s"); return; }
  const uint16_t drop = slots[shown == 0 ? 1 : 0].conn;
  NimBLEConnInfo info = srv->getPeerInfoByHandle(drop);
  kickedPeer = info.getIdAddress();
  srv->disconnect(drop);
  Serial.printf("[ble] pair window open 60 s, refusing %s\n", kickedPeer.toString().c_str());
}

bool Media::pairWindow() { return inPairWindow(); }

void Media::forgetPairing() {
  NimBLEDevice::deleteAllBonds();
  newAddress();
  prefs.end();
  Serial.println("[ble] bonds wiped, new address - rebooting");
  delay(200);
  ESP.restart();
}

String Media::address() {
  char b[18];
  snprintf(b, sizeof b, "%02X:%02X:%02X:%02X:%02X:%02X", ownAddr[5], ownAddr[4], ownAddr[3], ownAddr[2], ownAddr[1], ownAddr[0]);
  return String(b);
}
