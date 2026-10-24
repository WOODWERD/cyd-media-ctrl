#!/bin/bash
# Stop the helper until the next login (frees the USB port, e.g. for flashing). ./start.sh resumes it.
launchctl bootout "gui/$(id -u)/com.woodwerd.mediactrl" 2>/dev/null && echo "Helper stopped." || echo "Helper wasn't running."
