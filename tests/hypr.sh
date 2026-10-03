#!/bin/bash
# Run as the test user: hyprctl against session DISPLAY
# Usage: hypr.sh DISPLAY hyprctl-args...
export XDG_RUNTIME_DIR=/run/user/$(id -u)
export HYPRLAND_INSTANCE_SIGNATURE=$(<"$XDG_RUNTIME_DIR/omarchy-xrdp/$1/hyprland-instance")
shift
hyprctl "$@"
