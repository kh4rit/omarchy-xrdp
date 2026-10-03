#!/bin/bash
# Run as the test user: kill session DISPLAY's wayvnc or VNC proxy, to test
# that the launcher restarts it. Usage: kill-piece.sh DISPLAY wayvnc|proxy
export XDG_RUNTIME_DIR=/run/user/$(id -u)
state=$XDG_RUNTIME_DIR/omarchy-xrdp/$1
case $2 in
  wayvnc) pkill -u "$(id -u)" -f "wayvnc .*--socket=$state/wayvnc-control.sock" ;;
  proxy) pkill -u "$(id -u)" -f "omarchy-xrdp-vnc-proxy .*$state/wayvnc.sock" ;;
esac
