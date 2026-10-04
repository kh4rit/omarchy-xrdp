#!/bin/bash
# Run as the test user: start the staged omarchy-xrdp-backend against test
# session DISPLAY's Hyprland, listening on SOCKET (for a private xrdp).
# Usage: backend-run.sh DISPLAY SOCKET
stage=${STAGE:-/tmp/omxrdp-test}
export XDG_RUNTIME_DIR=/run/user/$(id -u)
state=$XDG_RUNTIME_DIR/omarchy-xrdp/$1
export WAYLAND_DISPLAY=$(<"$state/wayland")
export HYPRLAND_INSTANCE_SIGNATURE=$(<"$state/hyprland-instance")
export OMARCHY_XRDP_DISPLAY=$1
exec "$stage/lib/omarchy-xrdp-backend" -v --socket "$2" --resize-cmd "$stage/lib/omarchy-xrdp-resize" \
  >"$stage/backend-$1.log" 2>&1
