#!/bin/bash
# Run as the test user: show whether test session DISPLAY is still running.
# With WAIT=N, wait up to N seconds for it to stop first.
export XDG_RUNTIME_DIR=/run/user/$(id -u)
unit=omxrdp-test-$1
for ((i = 0; i < ${WAIT:-0}; i++)); do
  systemctl --user is-active --quiet "$unit" || break
  sleep 1
done
echo "unit $unit: $(systemctl --user is-active "$unit")"
echo "state dir: $([[ -d $XDG_RUNTIME_DIR/omarchy-xrdp/$1 ]] && echo present || echo gone)"
echo "processes: $(pgrep -u "$(id -u)" -fc 'omarchy-xrdp/'"$1"'/hyprland.lua|omarchy-xrdp-session :'"$1"'|vnc-'"$1"'.sock|Xwayland :'"$1")"
