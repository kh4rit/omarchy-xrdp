#!/bin/bash
# Run as the test user: show the launcher's own lines from session DISPLAY's
# log (skipping cage/Hyprland/Xwayland chatter). Usage: log.sh DISPLAY [LINES]
log=${XDG_STATE_HOME:-$HOME/.local/state}/omarchy-xrdp/session-$1.log
grep -E '^(----|[0-9]{2}:[0-9]{2}:[0-9]{2} )' "$log" | tail -"${2:-20}"
