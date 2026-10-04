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
as_user() { if [[ $user == "$(id -un)" ]]; then "$@"; else sudo -n -u "$user" "$@"; fi; }
target() { as_user "$stage/tests/console-target.sh" "$@"; }
monitors() { target hyprctl -j monitors; }
rdp_size() { monitors | jq -r '.[] | select(.name == "HEADLESS-RDP") | "\(.width)x\(.height)"'; }
# This run's part of the session log
session_log() {
  as_user cat "/home/$user/.local/state/omarchy-xrdp/session-20.log" | awk '/^----/ { n = 0 } { l[n++] = $0 } END { for (i = 0; i < n; i++) print l[i] }'
}
gtk_theme=$(as_user dconf read /org/gnome/desktop/interface/cursor-theme)
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
as_user env OMARCHY_XRDP_ATTACH="$sig" OMARCHY_XRDP_LOCK="touch $target_dir/locked" OMARCHY_XRDP_NO_SOUND=1 \
  "$stage/start" 20 1600x900 >/dev/null
check "console session starts" "1600x900" "$(rdp_size)"
check "local monitor is dark" "false" \
  "$(monitors | jq -r --arg m "$local_mon" '.[] | select(.name == $m) | .dpmsStatus')"
check "workspace on the RDP monitor" "true" \
  "$(monitors | jq -r '.[] | select(.name == "HEADLESS-RDP") | .activeWorkspace.id > 0')"
as_user chmod 666 "$stage/xup-20.sock"

client_size=${CLIENT_SIZE:-1280x720}
RDP_CLIENT=xfreerdp3 "$client" start "127.0.0.1:${PORT:-13390}" "$client_size" /dynamic-resolution >/dev/null
sleep 9
check "resizes to the client" "$client_size" "$(rdp_size)"
# The first input only focuses the client window
"$client" xtype mousemove 300 300
sleep 2
"$client" xtype key super+Return
"$client" xtype mousemove 400 350
sleep 3
check "remote keys reach the desktop" "yes" "$(as_user test -e "$target_dir/remote-key" && echo yes)"
check "remote input isn't local input" "yes" "$(session_live)"

# The client draws the cursor; the backend follows its shape (needs
# BACKEND_DEBUG=1 in the test user's xrdp.conf for the log lines)
debug=$(session_log | grep -q 'input activity, ' && echo yes)  # a debug-only line
[[ -n $debug ]] && check "hidden cursor theme in use" "yes" \
  "$(session_log | grep -q 'cursor shape default' && echo yes)"
target hyprctl dispatch 'hl.dsp.exec_cmd("foot")' >/dev/null
sleep 2
"$client" xtype mousemove 640 360
sleep 0.5
"$client" xtype mousemove 660 380
sleep 2
if [[ -n $debug ]]; then
check "text cursor over a terminal" "yes" "$(session_log | grep -q 'cursor shape text' && echo yes)"
check "cursor image at the session scale" "${CURSOR_PX:-24}x${CURSOR_PX:-24}" \
  "$(session_log | sed -n 's/.*cursor shape text (\(.*\))/\1/p' | tail -1)"
fi
check "GTK cursor theme untouched" "$gtk_theme" "$(as_user dconf read /org/gnome/desktop/interface/cursor-theme)"

# MAC_SHORTCUTS=yes in the test user's xrdp.conf: Windows App's Ctrl+V burst
# (its Command+V) becomes Super+V; a real (slow) Control+C stays Ctrl+C
if session_log | grep -q 'Mac shortcuts:'; then
  out=$stage/mac-typed-$RANDOM
  "$client" xtype type "cat > $out"
  "$client" xtype key Return
  sleep 0.5
  "$client" xtype keydown ctrl keydown v keyup v keyup ctrl
  sleep 1
  check "Command+V (Ctrl+V burst) arrives as Super+V" "yes" \
    "$(as_user test -e "$target_dir/super-v" && echo yes)"
  "$client" xtype type "x"
  "$client" xtype key Return
  sleep 0.3
  "$client" xtype keydown ctrl
  sleep 0.3
  "$client" xtype keydown c
  sleep 0.15
  "$client" xtype keyup c
  sleep 0.2
  "$client" xtype keyup ctrl
  sleep 0.5
  "$client" xtype type "still running"
  "$client" xtype key Return
  sleep 1
  check "a real Control+C still interrupts" "x" "$(as_user cat "$out" 2>/dev/null)"
  as_user rm -f "$out"
fi

target hyprctl reload >/dev/null
sleep 3
check "size kept across a config reload" "$client_size" "$(rdp_size)"
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
