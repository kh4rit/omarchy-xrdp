#!/bin/bash
# Run as the test user: point the systemd --user environment back at the
# live omarchy-xrdp session on DISPLAY (after a test session overwrote it),
# copying the session variables from that session's Hyprland process.
# Usage: restore-env.sh DISPLAY
export XDG_RUNTIME_DIR=/run/user/$(id -u)
sig=$(<"$XDG_RUNTIME_DIR/omarchy-xrdp/$1/hyprland-instance")
pid=$(head -1 "$XDG_RUNTIME_DIR/hypr/$sig/hyprland.lock")
vars=()
while IFS= read -r -d '' kv; do
  case ${kv%%=*} in
    DISPLAY | XDG_CURRENT_DESKTOP | \
      XDG_SESSION_DESKTOP | XDG_SESSION_TYPE | OMARCHY_XRDP_DISPLAY | XRDP_SESSION | XRDP_SOCKET_PATH | \
      XRDP_PULSE_SINK_SOCKET | XRDP_PULSE_SOURCE_SOCKET) vars+=("$kv") ;;
  esac
done </proc/"$pid"/environ
# Hyprland's own socket and X display are set for its children, not itself
vars+=("WAYLAND_DISPLAY=$(sed -n 2p "$XDG_RUNTIME_DIR/hypr/$sig/hyprland.lock")")
vars+=("HYPRLAND_INSTANCE_SIGNATURE=$sig")
printf "setting: %q\n" "${vars[@]}" >&2
systemctl --user set-environment "${vars[@]}"
systemctl --user show-environment | grep -E '^(WAYLAND_DISPLAY|DISPLAY|HYPRLAND_INSTANCE_SIGNATURE|OMARCHY_XRDP_DISPLAY)='
