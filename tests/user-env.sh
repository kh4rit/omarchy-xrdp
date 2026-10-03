#!/bin/bash
# Run as the test user: show session-related variables in the systemd --user
# manager's environment; with --clean, unset the ones a test session leaves
# behind. Usage: user-env.sh [--clean]
export XDG_RUNTIME_DIR=/run/user/$(id -u)
vars='^(WAYLAND_DISPLAY|DISPLAY|HYPRLAND_INSTANCE_SIGNATURE|HYPRLAND_CMD|XDG_CURRENT_DESKTOP|XDG_SESSION_DESKTOP|XDG_SESSION_TYPE|OMARCHY_XRDP_[A-Z_]*|XRDP_[A-Z_]*|LD_LIBRARY_PATH|PATH)='
if [[ ${1:-} == --clean ]]; then
  names=$(systemctl --user show-environment | grep -E "$vars" | cut -d= -f1 | grep -vx PATH)
  [[ -n $names ]] && systemctl --user unset-environment $names
  # Drop test directories from PATH, keep the rest
  path=$(systemctl --user show-environment | sed -n 's/^PATH=//p' | tr ':' '\n' | grep -v '^/tmp/omxrdp' | paste -sd:)
  [[ -n $path ]] && systemctl --user set-environment PATH="$path"
fi
systemctl --user show-environment | grep -E "$vars" | cut -c1-160
