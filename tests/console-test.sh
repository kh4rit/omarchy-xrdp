#!/bin/bash
# Console mode regression (run as the developer, private xrdp on $PORT):
# attach an RDP session to a running desktop, check its monitors and input,
# that remote input doesn't count as local, and that local input ends the
# remote session and locks the desktop.
# Usage: console-test.sh TEST_USER
set -u
user=$1
stage=${STAGE:-/tmp/omxrdp-test}
client=$stage/tests/rdp-client.sh
uid=$(id -u "$user")
target_dir=$stage/console-target-$uid
as_user() { sudo -n -u "$user" "$@"; }
target() { as_user "$stage/tests/console-target.sh" "$@"; }
monitors() { target hyprctl -j monitors; }
rdp_size() { monitors | jq -r '.[] | select(.name == "HEADLESS-RDP") | "\(.width)x\(.height)"'; }
session_live() { as_user test -e "/run/user/$uid/omarchy-xrdp/20/pid" && echo yes || echo no; }

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
target stop
target start >/dev/null || exit 1
local_mon=LOCAL-1
sig=$(as_user cat "$target_dir/sig")
as_user env OMARCHY_XRDP_ATTACH="$sig" OMARCHY_XRDP_LOCK="touch $target_dir/locked" \
  "$stage/start" 20 1600x900 >/dev/null
check "console session starts" "1600x900" "$(rdp_size)"
check "local monitor is dark" "false" \
  "$(monitors | jq -r --arg m "$local_mon" '.[] | select(.name == $m) | .dpmsStatus')"
check "workspace on the RDP monitor" "true" \
  "$(monitors | jq -r '.[] | select(.name == "HEADLESS-RDP") | .activeWorkspace.id > 0')"
as_user chmod 666 "$stage/xup-20.sock"

RDP_CLIENT=xfreerdp3 "$client" start "127.0.0.1:${PORT:-13390}" 1280x720 /dynamic-resolution >/dev/null
sleep 9
check "resizes to the client" "1280x720" "$(rdp_size)"
# The first input only focuses the client window
"$client" xtype mousemove 300 300
sleep 2
"$client" xtype key super+Return
"$client" xtype mousemove 400 350
sleep 3
check "remote keys reach the desktop" "yes" "$(as_user test -e "$target_dir/remote-key" && echo yes)"
check "remote input isn't local input" "yes" "$(session_live)"

target hyprctl reload >/dev/null
sleep 3
check "size kept across a config reload" "1280x720" "$(rdp_size)"
check "input still doesn't wake the local monitor" "0" \
  "$(target hyprctl -j getoption misc:key_press_enables_dpms | jq -r 'if .bool == true or .int == 1 then 1 else 0 end')"
check "reload isn't local input" "yes" "$(session_live)"

sleep 2 # idle first, so the next input is noticed
target local-input
sleep 4
check "local input ends the session" "no" "$(session_live)"
check "RDP monitor removed" "" "$(rdp_size)"
check "local monitor back on" "true" \
  "$(monitors | jq -r --arg m "$local_mon" '.[] | select(.name == $m) | .dpmsStatus')"
check "desktop locked" "yes" "$(as_user test -e "$target_dir/locked" && echo yes)"
check "input wakes the screens again" "1" \
  "$(target hyprctl -j getoption misc:key_press_enables_dpms | jq -r 'if .bool == true or .int == 1 then 1 else 0 end')"

"$client" stop
as_user "$stage/stop" 20 2>/dev/null
target stop
echo "$failures failure(s)"
exit $((failures > 0))
