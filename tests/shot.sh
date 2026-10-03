#!/bin/bash
# Run as the test user: screenshot the RDP-1 output of session DISPLAY
# Usage: shot.sh DISPLAY OUT.png
export XDG_RUNTIME_DIR=/run/user/$(id -u)
s=$XDG_RUNTIME_DIR/omarchy-xrdp/$1
WAYLAND_DISPLAY=$(<"$s/wayland") grim -o RDP-1 "$2" && chmod 644 "$2"
