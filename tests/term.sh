#!/bin/bash
# Run as the test user: SIGTERM session DISPLAY's launcher only, the way
# xrdp-sesman stops an X server
export XDG_RUNTIME_DIR=/run/user/$(id -u)
kill -TERM "$(<"$XDG_RUNTIME_DIR/omarchy-xrdp/$1/pid")"
