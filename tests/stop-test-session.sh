#!/bin/bash
# Run as the test user: stop an emulated session started by start-test-session.sh
export XDG_RUNTIME_DIR=/run/user/$(id -u)
systemctl --user stop "omxrdp-test-$1"
