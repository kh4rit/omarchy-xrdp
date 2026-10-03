#!/bin/bash
# Run as the test user: grep session DISPLAY's Hyprland log
# Usage: hypr-log.sh DISPLAY REGEX [LINES]
export XDG_RUNTIME_DIR=/run/user/$(id -u)
sig=$(<"$XDG_RUNTIME_DIR/omarchy-xrdp/$1/hyprland-instance")
sed 's/\x1b\[[0-9;]*m//g' "$XDG_RUNTIME_DIR/hypr/$sig/hyprland.log" | grep -E "$2" | tail -"${3:-30}" | cut -c1-200
