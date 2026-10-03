#!/bin/bash
# Launched inside a test session: record what launched commands see
{
  echo "uwsm: $(command -v uwsm)"
  env | grep -E '^(PATH|OMARCHY_XRDP|HYPRLAND_INSTANCE|XDG_RUNTIME|WAYLAND_DISPLAY|DISPLAY)=' | sort
} >/tmp/omxrdp-test/session-env.txt 2>&1
