#!/bin/bash
# Paste regression (run as the developer, private xrdp on $PORT): text the
# RDP client copied, pasted with Super+V (Command+V on a Mac) into a
# terminal and into a non-terminal window.
# Usage: paste-test.sh TEST_USER
set -u
user=$1
stage=${STAGE:-/tmp/omxrdp-test}
client=$stage/tests/rdp-client.sh
as_user() { sudo -n -u "$user" "$@"; }
hypr() { as_user "$stage/tests/hypr.sh" 20 "$@"; }
xclip_as_user() {
  as_user env DISPLAY=:20 LD_LIBRARY_PATH="$stage/tools/usr/lib" "$stage/tools/usr/bin/xclip" "$@"
}
out=$stage/pasted-$RANDOM

failures=0
check() {
  if [[ $3 == "$2" ]]; then echo "PASS $1"; else echo "FAIL $1: expected '$2', got '$3'"; ((failures++)); fi
}

"$client" stop
as_user "$stage/stop" 20 2>/dev/null
as_user "$stage/start" 20 1600x900 >/dev/null || exit 1
as_user chmod 666 "$stage/xup-20.sock"
RDP_CLIENT=xfreerdp3 "$client" start "127.0.0.1:${PORT:-13390}" 1280x720 /dynamic-resolution >/dev/null
sleep 9
"$client" xtype mousemove 300 300
sleep 1

# The client copies (chansrv owns the X clipboard; xclip stands in)
text="pasted from the client $RANDOM"
printf '%s' "$text" | xclip_as_user -selection clipboard -i
sleep 1

# A terminal (Omarchy's universal paste sends it Shift+Insert)
"$client" xtype key super+Return
sleep 3
"$client" xtype mousemove 640 360
sleep 0.5
"$client" xtype type "cat > $out"
"$client" xtype key Return
sleep 0.5
echo "active window: $(hypr -j activewindow | jq -c '{class, tags}')"
echo "session clipboard: '$(as_user env XDG_RUNTIME_DIR=/run/user/$(id -u "$user") \
  WAYLAND_DISPLAY="$(as_user cat /run/user/$(id -u "$user")/omarchy-xrdp/20/wayland)" wl-paste -n 2>&1)'"
"$client" xtype type "A"
"$client" xtype key super+v
sleep 1
"$client" xtype type "B"
"$client" xtype key Return
sleep 0.3
"$client" xtype key ctrl+d
sleep 1
check "Super+V pastes into a terminal" "A${text}B" "$(as_user cat "$out" 2>/dev/null)"
as_user rm -f "$out"

"$client" stop
as_user "$stage/stop" 20 2>/dev/null
echo "$failures failure(s)"
exit $((failures > 0))
