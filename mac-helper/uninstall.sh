#!/bin/bash
# Remove the Media Ctrl helper and its LaunchAgent.
LABEL=com.woodwerd.mediactrl
launchctl bootout "gui/$(id -u)/$LABEL" 2>/dev/null || true
rm -f "$HOME/Library/LaunchAgents/$LABEL.plist"
rm -rf "$HOME/Library/Application Support/MediaCtrl"
echo "Media Ctrl helper removed. (The log at ~/Library/Logs/MediaCtrl.log is left in place.)"
