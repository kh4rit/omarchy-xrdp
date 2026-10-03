#!/bin/bash
# Run as the test user: read (no args) or set (with TEXT) the Wayland
# clipboard of session DISPLAY. Usage: clipboard.sh DISPLAY [TEXT]
export XDG_RUNTIME_DIR=/run/user/$(id -u)
export WAYLAND_DISPLAY=$(<"$XDG_RUNTIME_DIR/omarchy-xrdp/$1/wayland")
if [[ -n ${2:-} ]]; then
  printf '%s' "$2" | wl-copy
else
  wl-paste --no-newline
fi
