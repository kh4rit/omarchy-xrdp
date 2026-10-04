#!/bin/bash
# Run as the test user: capture one frame and the cursor image of session
# DISPLAY into $STAGE/dump (screen.ppm, cursor.ppm over magenta).
# Usage: dump-capture.sh DISPLAY
stage=${STAGE:-/tmp/omxrdp-test}
export XDG_RUNTIME_DIR=/run/user/$(id -u)
export WAYLAND_DISPLAY=$(<"$XDG_RUNTIME_DIR/omarchy-xrdp/$1/wayland")
mkdir -p "$stage/dump"
chmod 777 "$stage/dump" 2>/dev/null
"$stage/lib/omarchy-xrdp-backend" --bench 2 --dump "$stage/dump"
chmod 644 "$stage"/dump/*.ppm 2>/dev/null
