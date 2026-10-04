#!/bin/bash
# Run as the test user: start a console-mode test session on DISPLAY that
# attaches to the Hyprland of test session TARGET (standing in for a live
# local desktop), with its own monitor name so it can't clash.
# Usage: start-console.sh DISPLAY TARGET [WxH]
stage=${STAGE:-/tmp/omxrdp-test}
export XDG_RUNTIME_DIR=/run/user/$(id -u)
export OMARCHY_XRDP_ATTACH=$(<"$XDG_RUNTIME_DIR/omarchy-xrdp/$2/hyprland-instance")
export OMARCHY_XRDP_OUTPUT=HEADLESS-CONSOLE
exec "$stage/start" "$1" "${3:-1280x720}"
