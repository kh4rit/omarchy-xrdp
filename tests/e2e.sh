#!/bin/bash
# End-to-end regression (run as the developer, with sudo -u to the test user
# allowed and the private xrdp from the README listening on 13389):
# session start, RDP connect + resize, keyboard and Omarchy keybindings,
# mid-session resize, config reload, Omarchy logout and cleanup.
# Usage: e2e.sh TEST_USER [START_WRAPPER]   (default wrapper: $STAGE/start)
set -u
user=$1
stage=${STAGE:-/tmp/omxrdp-test}
start=${2:-$stage/start}
client=$stage/tests/rdp-client.sh
as_user() { sudo -n -u "$user" "$@"; }
size() { as_user "$stage/tests/hypr.sh" 20 -j monitors | jq -r '.[0] | "\(.width)x\(.height)"'; }

failures=0
check() {
  local name=$1 expected=$2 actual=$3
  if [[ $actual == "$expected" ]]; then
    echo "PASS $name"
  else
    echo "FAIL $name: expected '$expected', got '$actual'"
    ((failures++))
  fi
}

"$client" stop
as_user "$stage/stop" 20 2>/dev/null
as_user "$start" 20 1600x900 >/dev/null
check "session starts" "1600x900" "$(size)"
as_user chmod 666 "$stage/vnc-20.sock"

RDP_CLIENT=xfreerdp3 "$client" start 127.0.0.1:13389 1280x720 /dynamic-resolution >/dev/null
sleep 9
check "resizes to the client at connect" "1280x720" "$(size)"

"$client" xtype key super+Return
sleep 2
check "Super+Return opens a terminal" "1" "$(as_user "$stage/tests/hypr.sh" 20 -j clients | jq length)"

marker=$stage/e2e-typed
rm -f "$marker"
"$client" xtype type --delay 40 "echo typed-over-rdp > $marker"
"$client" xtype key Return
sleep 1.5
check "typing reaches the session" "typed-over-rdp" "$(cat "$marker" 2>/dev/null)"

"$client" resize 1920x1080
sleep 8
check "follows a mid-session resize" "1920x1080" "$(size)"

as_user "$stage/tests/hypr.sh" 20 reload >/dev/null
sleep 2
check "keeps the size across a config reload" "1920x1080" "$(size)"

"$client" shot "$stage/e2e-final.png"
check "client shows a picture" "yes" "$( (($(stat -c %s "$stage/e2e-final.png") > 100000)) && echo yes || echo no)"

as_user "$stage/tests/launch.sh" 20 omarchy-system-logout >/dev/null
sleep 10
status=$(as_user "$stage/tests/status.sh" 20)
check "Omarchy logout ends the session" "unit omxrdp-test-20: inactive" "$(head -1 <<<"$status")"
check "logout cleans up state" "state dir: gone" "$(sed -n 2p <<<"$status")"
check "logout leaves no processes" "processes: 0" "$(sed -n 3p <<<"$status")"

"$client" stop
echo "$failures failure(s)"
exit $((failures > 0))
