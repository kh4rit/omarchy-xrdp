#!/bin/bash
# Run as the test user: evaluate Lua in session DISPLAY's Hyprland, read from
# stdin (keeps quoting out of sudo command lines). Usage: eval.sh DISPLAY < code
export XDG_RUNTIME_DIR=/run/user/$(id -u)
export HYPRLAND_INSTANCE_SIGNATURE=$(<"$XDG_RUNTIME_DIR/omarchy-xrdp/$1/hyprland-instance")
hyprctl eval "$(cat)"
