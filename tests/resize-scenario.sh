#!/bin/bash
# Dev helper (run as the developer, with sudo -u to the test user allowed):
# start a fresh 1600x900 test session, connect the test RDP client at SIZE
# through the private xrdp, and report whether the session followed.
# Usage: resize-scenario.sh TEST_USER SIZE [ROUNDS]
set -u
user=$1 size=$2 rounds=${3:-1}
stage=${STAGE:-/tmp/omxrdp-test}
xrdp_log=${XRDP_LOG:?set XRDP_LOG to the private xrdp log}

for ((r = 1; r <= rounds; r++)); do
  "$stage/tests/rdp-client.sh" stop
  sudo -n -u "$user" "$stage/stop" 20
  sudo -n -u "$user" "$stage/start" 20 1600x900 >/dev/null || { echo "round $r: session failed"; continue; }
  sudo -n -u "$user" chmod 666 "$stage/xup-20.sock"
  mark=$(wc -l <"$xrdp_log")
  RDP_CLIENT=xfreerdp3 "$stage/tests/rdp-client.sh" start 127.0.0.1:${PORT:-13390} "$size" /dynamic-resolution >/dev/null
  sleep 9
  got=$(sudo -n -u "$user" "$stage/tests/hypr.sh" 20 -j monitors | jq -r '.[0] | "\(.width)x\(.height)"')
  result=$(tail -n +"$mark" "$xrdp_log" | grep -oE 'successfully resized|resize failed - error code [0-9]+' | tr '\n' ',' )
  echo "round $r: session $got, xrdp: ${result%,}"
  if [[ -n ${STOP_ON_FAIL:-} && $got != "$size" ]]; then
    echo "stopping with the failed session and client still running"
    exit 1
  fi
done
"$stage/tests/rdp-client.sh" stop
