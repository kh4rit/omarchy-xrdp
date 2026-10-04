#!/bin/bash
# Frame rate benchmark (run as the developer, private xrdp on $PORT): a
# console session on a throwaway Hyprland, with its own xrdp.conf
# (FRAME_RATE, BACKEND_DEBUG), a full-screen scrolling terminal, and the
# backend's frames/s and xrdp's CPU over SECONDS.
# Usage: fps-test.sh WxH RATE [SECONDS]
set -u
size=$1 rate=$2 secs=${3:-15}
stage=${STAGE:-/tmp/omxrdp-test}
client=$stage/tests/rdp-client.sh
uid=$(id -u)
target() { "$stage/tests/console-target.sh" "$@"; }
conf=$stage/fps-config-$uid

"$client" stop
"$stage/stop" 20 2>/dev/null
target stop
target start >/dev/null || exit 1
mkdir -p "$conf/omarchy"
printf 'FRAME_RATE=%s\nBACKEND_DEBUG=1\n' "$rate" >"$conf/omarchy/xrdp.conf"

OMARCHY_XRDP_ATTACH=$(<"$stage/console-target-$uid/sig") XDG_CONFIG_HOME=$conf \
  OMARCHY_XRDP_LOCK=true OMARCHY_XRDP_NO_SOUND=1 "$stage/start" 20 "$size" >/dev/null
chmod 666 "$stage/xup-20.sock"
RDP_CLIENT=xfreerdp3 "$client" start "127.0.0.1:${PORT:-13390}" "$size" /dynamic-resolution >/dev/null
sleep 8
echo "monitor: $(target hyprctl -j monitors | jq -r '.[] | select(.name == "HEADLESS-RDP") | "\(.width)x\(.height)@\(.refreshRate) scale \(.scale)"')"

target hyprctl dispatch "hl.dsp.exec_cmd(\"foot sh $stage/tests/load.sh\")" >/dev/null
sleep 2
target hyprctl dispatch 'hl.dsp.window.fullscreen()' >/dev/null
sleep 2

xrdp_pid=$(pgrep -u "$uid" -nx xrdp)
client_pid=$(pgrep -u "$uid" -nx xfreerdp3)
ticks() { awk '{print $14 + $15}' "/proc/$1/stat" 2>/dev/null || echo 0; }
c0=$(ticks "$xrdp_pid") k0=$(ticks "$client_pid")
sleep "$secs"
c1=$(ticks "$xrdp_pid") k1=$(ticks "$client_pid")
echo "xrdp CPU: $(( (c1 - c0) * 100 / $(getconf CLK_TCK) / secs ))%, test client CPU: $(( (k1 - k0) * 100 / $(getconf CLK_TCK) / secs ))% (of one core)"
awk '/^----/ { n = 0 } { l[n++] = $0 } END { for (i = 0; i < n; i++) print l[i] }' \
  "$HOME/.local/state/omarchy-xrdp/session-20.log" | grep -a 'frames/s sent' | tail -3

"$client" stop
"$stage/stop" 20 2>/dev/null
target stop
