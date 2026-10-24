#!/bin/bash
# Start the helper again after ./stop.sh.
PLIST="$HOME/Library/LaunchAgents/com.woodwerd.mediactrl.plist"
[ -f "$PLIST" ] || { echo "Not installed — run ./install.sh first."; exit 1; }
launchctl bootstrap "gui/$(id -u)" "$PLIST" 2>/dev/null && echo "Helper started." || echo "Helper already running."
