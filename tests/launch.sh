#!/bin/bash
# Run as the test user: launch a command inside session DISPLAY's Hyprland
# Usage: launch.sh DISPLAY COMMAND
export XDG_RUNTIME_DIR=/run/user/$(id -u)
export HYPRLAND_INSTANCE_SIGNATURE=$(<"$XDG_RUNTIME_DIR/omarchy-xrdp/$1/hyprland-instance")
hyprctl dispatch "hl.dsp.exec_cmd(\"$2\")"
