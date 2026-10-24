#!/bin/bash
# Install the Media Ctrl helper for this user: copies it to ~/Library/Application Support/MediaCtrl
# and starts it at login with a LaunchAgent. Run from this folder:  ./install.sh
set -e
LABEL=com.woodwerd.mediactrl
DEST="$HOME/Library/Application Support/MediaCtrl"
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"
LOG="$HOME/Library/Logs/MediaCtrl.log"
SRC="$(cd "$(dirname "$0")" && pwd)"

if ! /usr/bin/python3 -c 'import sys; sys.exit(0 if sys.version_info >= (3, 8) else 1)' 2>/dev/null; then
  echo "Python 3 isn't set up yet. macOS will offer to install the Command Line Tools —"
  echo "accept, wait for it to finish, then run ./install.sh again."
  /usr/bin/python3 --version >/dev/null 2>&1 || true
  exit 1
fi

mkdir -p "$DEST" "$HOME/Library/LaunchAgents" "$HOME/Library/Logs"
rsync -a --delete --exclude install.sh --exclude uninstall.sh "$SRC/" "$DEST/"

cat > "$PLIST" <<PL
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key><string>$LABEL</string>
  <key>ProgramArguments</key>
  <array><string>/usr/bin/python3</string><string>$DEST/media_ctrl_helper.py</string></array>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><true/>
  <key>ProcessType</key><string>Interactive</string>
  <key>StandardOutPath</key><string>$LOG</string>
  <key>StandardErrorPath</key><string>$LOG</string>
</dict>
</plist>
PL

launchctl bootout "gui/$(id -u)/$LABEL" 2>/dev/null || true
launchctl bootstrap "gui/$(id -u)" "$PLIST"
echo "Media Ctrl helper installed and running."
echo "  First time Spotify or Music plays, macOS asks to let python3 control it: click OK."
echo "  Log:        $LOG"
echo "  Pause it (e.g. to flash the board):  ./stop.sh   ·   start again: ./start.sh"
