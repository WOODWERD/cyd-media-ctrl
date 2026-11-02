#!/usr/bin/env python3
"""Media Ctrl helper for macOS — WOODWERD LLC, MIT licence.

Feeds the Media Ctrl board (Cheap Yellow Display) with what Spotify or Apple Music is playing on
this Mac, over the board's USB cable, and carries its button presses back as AppleScript commands.
macOS doesn't publish Now Playing to Bluetooth accessories the way an iPad does; this fills the gap.

  * Finds the board on its own: every USB serial port is asked {"t":"who"}; only a Media Ctrl
    answers. Opened with DTR/RTS held low, so the board is not reset.
  * Reads Spotify and Music with AppleScript, only when they're already running (never launches
    them just to look), plus the system output volume and mute.
  * Sends one JSON line when something changes, and at least every 4 s so the board knows it's here.

Run it once from Terminal to check:  python3 media_ctrl_helper.py --once
Install it to start at login:       ./install.sh
"""
import argparse
import glob
import json
import os
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)  # vendored pyserial (serial/), BSD licence
import serial  # noqa: E402

VERSION = "0.2.0"
POLL_S = 1.0
KEEPALIVE_S = 4.0
PORT_GLOBS = ["/dev/cu.usbserial-*", "/dev/cu.wchusbserial*", "/dev/cu.SLAB_USBtoUART*", "/dev/cu.usbmodem*"]

# Command numbers used by the firmware (Media::Cmd, same as Apple Media Service RemoteCommandID)
PLAY, PAUSE, TOGGLE, NEXT, PREV, VOL_UP, VOL_DOWN, REPEAT, SHUFFLE, SKIP_FWD, SKIP_BACK, LIKE = range(12)
MUTE = 0xF0
SUPPORTED = {
    "Spotify": [PLAY, PAUSE, TOGGLE, NEXT, PREV, VOL_UP, VOL_DOWN, REPEAT, SHUFFLE, SKIP_FWD, SKIP_BACK, MUTE],
    "Music": [PLAY, PAUSE, TOGGLE, NEXT, PREV, VOL_UP, VOL_DOWN, REPEAT, SHUFFLE, SKIP_FWD, SKIP_BACK, LIKE, MUTE],
}
SKIP_S = 15
VOL_STEP = 6


def log(*a):
    print(time.strftime("%H:%M:%S"), *a, flush=True)


# ------------------------------------------------------------------ AppleScript
def osa(script, timeout=3.0):
    """Run AppleScript, return (ok, stdout)."""
    try:
        r = subprocess.run(["osascript", "-e", script], capture_output=True, text=True, timeout=timeout)
        return r.returncode == 0, r.stdout.strip()
    except (subprocess.TimeoutExpired, OSError) as e:
        return False, str(e)


def running(app):
    """True if the app is running, without launching it."""
    return subprocess.run(["pgrep", "-xq", app]).returncode == 0


READ = {
    "Spotify": '''tell application "Spotify"
set pstate to player state as text
if pstate is "stopped" then return "stopped"
set t to current track
return pstate & linefeed & (name of t) & linefeed & (artist of t) & linefeed & ((duration of t) / 1000) & linefeed & player position & linefeed & shuffling & linefeed & repeating
end tell''',
    "Music": '''tell application "Music"
set pstate to player state as text
if pstate is "stopped" then return "stopped"
try
  set t to current track
  set nm to name of t
  set ar to artist of t
  set du to duration of t
on error
  set nm to ""
  set ar to ""
  set du to 0
end try
return pstate & linefeed & nm & linefeed & ar & linefeed & du & linefeed & player position & linefeed & shuffle enabled & linefeed & (song repeat as text)
end tell''',
}


def num(s):
    """AppleScript reals follow the user's locale (decimal comma) and may use E-notation."""
    try:
        return float(s.strip().replace(",", "."))
    except (ValueError, AttributeError):
        return 0.0


def parse_app(app, out):
    lines = out.split("\n")
    st = lines[0].strip()
    if st == "stopped" or len(lines) < 7:
        return None
    state = 1 if st in ("playing", "fast forwarding", "rewinding") else 0
    shuffle = 2 if lines[5].strip() == "true" else 0
    rp = lines[6].strip()
    repeat = {"true": 2, "false": 0, "off": 0, "one": 1, "all": 2}.get(rp, 0)
    return {
        "app": app, "title": lines[1], "artist": lines[2],
        "dur": round(num(lines[3]), 1), "pos": round(num(lines[4]), 1),
        "state": state, "shuffle": shuffle, "repeat": repeat,
    }


def read_volume():
    ok, out = osa('set v to get volume settings\nreturn (output volume of v as text) & linefeed & (output muted of v as text)')
    if not ok:
        return -1, 0
    parts = out.split("\n")
    vol = int(num(parts[0])) if parts and parts[0] not in ("missing value", "") else -1
    muted = 1 if len(parts) > 1 and parts[1].strip() == "true" else 0
    return vol, muted


class Player:
    """Chooses which app is 'now playing' and runs commands against it."""

    def __init__(self):
        self.last_app = None

    def read(self):
        tracks = []
        for app in ("Spotify", "Music"):
            if running(app):
                ok, out = osa(READ[app])
                if ok:
                    t = parse_app(app, out)
                    if t:
                        tracks.append(t)
        pick = next((t for t in tracks if t["state"] == 1), None)
        if not pick and self.last_app:
            pick = next((t for t in tracks if t["app"] == self.last_app), None)
        if not pick and tracks:
            pick = tracks[0]
        vol, muted = read_volume()
        if pick:
            self.last_app = pick["app"]
            msg = {"t": "np", **pick, "vol": vol, "muted": muted, "sup": SUPPORTED[pick["app"]]}
        else:
            msg = {"t": "idle", "vol": vol, "muted": muted}
        return msg

    def command(self, c):
        if c == VOL_UP:
            return osa(f"set volume output volume ((output volume of (get volume settings)) + {VOL_STEP})")
        if c == VOL_DOWN:
            return osa(f"set volume output volume ((output volume of (get volume settings)) - {VOL_STEP})")
        if c == MUTE:
            return osa("set volume output muted (not (output muted of (get volume settings)))")
        app = self.last_app
        if not app or not running(app):
            # nothing loaded: play resumes the last app, or whichever is running
            app = next((a for a in ("Spotify", "Music") if running(a)), self.last_app)
            if not app or c not in (PLAY, TOGGLE):
                return False, "no player"
            return osa(f'tell application "{app}" to play')
        tell = f'tell application "{app}" to '
        simple = {PLAY: "play", PAUSE: "pause", TOGGLE: "playpause", NEXT: "next track", PREV: "previous track"}
        if c in simple:
            return osa(tell + simple[c])
        if c in (SKIP_FWD, SKIP_BACK):
            d = SKIP_S if c == SKIP_FWD else -SKIP_S
            return osa(f'tell application "{app}"\nset p to (player position) + ({d})\nif p < 0 then set p to 0\nset player position to p\nend tell')
        if c == SHUFFLE:
            prop = "shuffling" if app == "Spotify" else "shuffle enabled"
            return osa(f'tell application "{app}" to set {prop} to not {prop}')
        if c == REPEAT:
            if app == "Spotify":
                return osa('tell application "Spotify" to set repeating to not repeating')
            return osa('tell application "Music"\nif song repeat is off then\nset song repeat to all\nelse if song repeat is all then\nset song repeat to one\nelse\nset song repeat to off\nend if\nend tell')
        if c == LIKE and app == "Music":
            ok, out = osa('tell application "Music" to set favorited of current track to true')
            return (ok, out) if ok else osa('tell application "Music" to set loved of current track to true')
        return False, f"unsupported {c} for {app}"


# ------------------------------------------------------------------ serial link
def open_port(path):
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = path, 115200, 0.2
    s.dtr = False  # hold DTR/RTS low: the CYD's auto-reset circuit leaves the ESP32 running
    s.rts = False
    s.open()
    return s


def find_board(explicit=None):
    ports = [explicit] if explicit else sorted({p for g in PORT_GLOBS for p in glob.glob(g)})
    for path in ports:
        try:
            s = open_port(path)
        except (serial.SerialException, OSError) as e:
            log(f"skip {path}: {e}")
            continue
        deadline = time.time() + 4.0
        asked = 0.0
        buf = b""
        while time.time() < deadline:
            if time.time() - asked > 1.5:  # ask twice: the board may still be booting
                s.write(b'{"t":"who"}\n')
                asked = time.time()
            buf += s.read(256)
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                line = line.strip()
                if not line.startswith(b"{"):
                    continue
                try:
                    m = json.loads(line)
                except ValueError:
                    continue
                if m.get("t") == "hello" and m.get("dev") == "media-ctrl":
                    log(f"found Media Ctrl {m.get('fw', '?')} on {path}")
                    return s
        s.close()
    return None


class Link:
    def __init__(self, port, player):
        self.port, self.player = port, player
        self.lock = threading.Lock()
        self.alive = True

    def send(self, msg):
        data = (json.dumps(msg, ensure_ascii=False, separators=(",", ":")) + "\n").encode("utf-8")
        with self.lock:
            self.port.write(data)

    def reader(self, on_cmd):
        buf = b""
        while self.alive:
            try:
                chunk = self.port.read(256)
            except (serial.SerialException, OSError):
                self.alive = False
                break
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                line = line.strip()
                if not line.startswith(b"{"):
                    continue
                try:
                    m = json.loads(line)
                except ValueError:
                    continue
                if m.get("t") == "cmd":
                    on_cmd(int(m.get("c", -1)))
                elif m.get("t") == "hello":
                    on_cmd(None)  # board rebooted: resend everything


def run(args):
    player = Player()
    while True:
        port = find_board(args.port)
        if not port:
            time.sleep(3)
            continue
        link = Link(port, player)
        force = threading.Event()
        force.set()

        def on_cmd(c):
            if c is None:
                force.set()
                return
            ok, out = player.command(c)
            log(f"command {c}: {'ok' if ok else 'failed ' + out}")
            force.set()  # report the new state right away

        threading.Thread(target=link.reader, args=(on_cmd,), daemon=True).start()
        try:
            link.send({"t": "host", "name": os.uname().nodename.split(".")[0] or "Mac"})
            last, last_sent = None, 0.0
            while link.alive:
                msg = player.read()
                now = time.time()
                if force.is_set() or msg != last or now - last_sent > KEEPALIVE_S:
                    force.clear()
                    link.send(msg)
                    last, last_sent = msg, now
                force.wait(POLL_S)
        except (serial.SerialException, OSError) as e:
            log(f"link lost: {e}")
        link.alive = False
        try:
            port.close()
        except Exception:
            pass
        log("board gone; looking again")
        time.sleep(2)


def main():
    ap = argparse.ArgumentParser(description="Media Ctrl helper for macOS")
    ap.add_argument("--port", help="serial port (default: find the board automatically)")
    ap.add_argument("--once", action="store_true", help="print what would be sent to the board and exit")
    args = ap.parse_args()
    if args.once:
        print(json.dumps(Player().read(), ensure_ascii=False, indent=2))
        return
    log(f"Media Ctrl helper {VERSION}")
    try:
        run(args)
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
