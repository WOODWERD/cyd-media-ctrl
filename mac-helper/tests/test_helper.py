"""Tests for the Mac helper that run anywhere (no macOS needed).

AppleScript and pgrep are replaced by a fake Mac; the serial link is a real pseudo-terminal pair with a
fake board on the other end that speaks the firmware's protocol. Run:  python3 -m pytest mac-helper/tests
or simply:  python3 mac-helper/tests/test_helper.py
"""
import json
import os
import pty
import sys
import threading
import time
import tty

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import media_ctrl_helper as H  # noqa: E402


class FakeMac:
    """Stands in for osascript + pgrep: Spotify and Music state, system volume, and a command log."""

    def __init__(self):
        self.apps = {
            "Spotify": {"running": True, "state": "playing", "name": "Low Tide Static, Pt. 2", "artist": "The Hollow Pines",
                        "dur_ms": 253000, "pos": 102.4, "shuffle": True, "repeat": False},
            "Music": {"running": True, "state": "paused", "name": "Weightless", "artist": "Señor Coconut",
                      "dur": 245.5, "pos": 10.0, "shuffle": False, "repeat": "one"},
        }
        self.volume, self.muted = 62, False
        self.calls = []
        self.decimal_comma = False

    def fmt(self, x):
        s = repr(float(x))
        return s.replace(".", ",") if self.decimal_comma else s

    def osa(self, script, timeout=3.0):
        self.calls.append(script)
        if script.startswith("set v to get volume settings"):
            return True, f"{self.volume}\n{'true' if self.muted else 'false'}"
        if "set volume output volume" in script:
            self.volume = max(0, min(100, self.volume + (6 if "+ 6" in script else -6)))
            return True, ""
        if "set volume output muted" in script:
            self.muted = not self.muted
            return True, ""
        for app, a in self.apps.items():
            if f'tell application "{app}"' not in script:
                continue
            if script == H.READ[app]:
                if a["state"] == "stopped":
                    return True, "stopped"
                dur = self.fmt(a["dur_ms"] / 1000) if app == "Spotify" else self.fmt(a["dur"])
                sh = "true" if a["shuffle"] else "false"
                rp = ("true" if a["repeat"] else "false") if app == "Spotify" else a["repeat"]
                return True, f"{a['state']}\n{a['name']}\n{a['artist']}\n{dur}\n{self.fmt(a['pos'])}\n{sh}\n{rp}"
            if script.endswith(" to playpause"):
                a["state"] = "paused" if a["state"] == "playing" else "playing"
            elif script.endswith(" to next track"):
                a["name"], a["pos"] = "Next Song", 0.0
            elif "player position" in script:
                a["pos"] = max(0.0, a["pos"] + (15 if "(15)" in script else -15))
            elif "shuffl" in script:
                a["shuffle"] = not a["shuffle"]
            elif "repeat" in script:
                if app == "Spotify":
                    a["repeat"] = not a["repeat"]
                else:
                    a["repeat"] = {"off": "all", "all": "one", "one": "off"}[a["repeat"]]
            elif "favorited" in script:
                a["liked"] = True
            elif script.endswith(" to play"):
                a["state"] = "playing"
            return True, ""
        return False, "unknown script"

    def running(self, app):
        return self.apps.get(app, {}).get("running", False)


def install(mac):
    H.osa = mac.osa
    H.running = mac.running


def test_read_picks_playing_app():
    mac = FakeMac(); install(mac)
    m = H.Player().read()
    assert m["t"] == "np" and m["app"] == "Spotify" and m["state"] == 1
    assert m["title"] == "Low Tide Static, Pt. 2" and m["dur"] == 253.0 and m["pos"] == 102.4
    assert m["shuffle"] == 2 and m["repeat"] == 0 and m["vol"] == 62 and H.MUTE in m["sup"]


def test_decimal_comma_locale():
    mac = FakeMac(); mac.decimal_comma = True; install(mac)
    m = H.Player().read()
    assert m["dur"] == 253.0 and m["pos"] == 102.4


def test_paused_fallbacks_and_idle():
    mac = FakeMac(); install(mac)
    mac.apps["Spotify"]["state"] = "paused"
    p = H.Player()
    p.last_app = "Music"
    m = p.read()
    assert m["app"] == "Music" and m["state"] == 0 and m["repeat"] == 1 and H.LIKE in m["sup"]
    mac.apps["Spotify"]["running"] = False
    mac.apps["Music"]["state"] = "stopped"
    assert p.read()["t"] == "idle"


def test_commands():
    mac = FakeMac(); install(mac)
    p = H.Player(); p.read()
    assert p.command(H.TOGGLE)[0] and mac.apps["Spotify"]["state"] == "paused"
    p.command(H.SKIP_FWD); assert mac.apps["Spotify"]["pos"] == 117.4
    p.command(H.SKIP_BACK); p.command(H.SKIP_BACK); assert mac.apps["Spotify"]["pos"] == 87.4
    p.command(H.SHUFFLE); assert mac.apps["Spotify"]["shuffle"] is False
    p.command(H.REPEAT); assert mac.apps["Spotify"]["repeat"] is True
    p.command(H.VOL_UP); assert mac.volume == 68
    p.command(H.MUTE); assert mac.muted
    assert p.command(H.LIKE)[0] is False                    # Spotify can't like from a script
    p.last_app = "Music"
    p.command(H.REPEAT); assert mac.apps["Music"]["repeat"] == "off"
    p.command(H.LIKE); assert mac.apps["Music"].get("liked")


def test_play_with_nothing_loaded_resumes():
    mac = FakeMac(); install(mac)
    for a in mac.apps.values():
        a["state"] = "stopped"
    p = H.Player()
    assert p.command(H.TOGGLE)[0] and mac.apps["Spotify"]["state"] == "playing"


class FakeBoard(threading.Thread):
    """The firmware's side of the USB link, on a pseudo-terminal."""

    def __init__(self, fd):
        super().__init__(daemon=True)
        self.fd, self.received, self.stop = fd, [], False

    def write(self, obj):
        os.write(self.fd, (json.dumps(obj) + "\n").encode())

    def run(self):
        os.write(self.fd, b"Media Ctrl v0.2.0\n#alive up=1 heap=150000\n")  # log noise the helper must ignore
        buf = b""
        while not self.stop:
            try:
                buf += os.read(self.fd, 1024)
            except OSError:
                break
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                m = json.loads(line)
                if m["t"] == "who":
                    self.write({"t": "hello", "dev": "media-ctrl", "fw": "0.2.0"})
                else:
                    self.received.append(m)


def test_end_to_end_over_serial():
    mac = FakeMac(); install(mac)
    H.POLL_S = 0.2
    master, slave = pty.openpty()
    tty.setraw(master); tty.setraw(slave)
    board = FakeBoard(master); board.start()
    port_path = os.ttyname(slave)
    t = threading.Thread(target=H.run, args=(type("A", (), {"port": port_path})(),), daemon=True)
    t.start()
    deadline = time.time() + 8
    while time.time() < deadline and not any(m["t"] == "np" for m in board.received):
        time.sleep(0.1)
    nps = [m for m in board.received if m["t"] == "np"]
    assert nps and nps[-1]["app"] == "Spotify" and nps[-1]["title"] == "Low Tide Static, Pt. 2"
    assert any(m["t"] == "host" for m in board.received)
    # a button press on the board
    n = len(board.received)
    board.write({"t": "cmd", "c": H.NEXT})
    deadline = time.time() + 4
    while time.time() < deadline and not any(m.get("title") == "Next Song" for m in board.received[n:]):
        time.sleep(0.1)
    assert any(m.get("title") == "Next Song" for m in board.received[n:]), "state after command not reported"
    # the exact line format the firmware parses
    with open(os.path.join(os.path.dirname(__file__), "sample_lines.jsonl"), "w") as f:
        for m in board.received[:6]:
            f.write(json.dumps(m, ensure_ascii=False, separators=(",", ":")) + "\n")
    board.stop = True


if __name__ == "__main__":
    for name, fn in list(globals().items()):
        if name.startswith("test_"):
            fn()
            print("ok ", name)
