#!/bin/bash
# H.264 corruption regression (run as the developer, private xrdp on $PORT):
# a console session on a throwaway Hyprland at WxH and RATE frames/s, two
# workspaces with a full-screen wallpaper each, ROUNDS fast switches, then
# compare what the client shows with what Hyprland rendered.
# Usage: artifact-test.sh [WxH] [RATE] [ROUNDS]
set -u
size=${1:-3840x2160} rate=${2:-120} rounds=${3:-15}
stage=${STAGE:-/tmp/omxrdp-test}
client=$stage/tests/rdp-client.sh
uid=$(id -u)
target() { "$stage/tests/console-target.sh" "$@"; }
conf=$stage/artifact-config-$uid
out=$stage/artifact-$uid
mkdir -p "$out"

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

# A detailed full-screen picture on each of two workspaces
if [[ -n ${PICS:-} ]]; then read -ra pics <<<"$PICS"; else
  mapfile -t pics < <(ls /usr/share/omarchy/themes/*/backgrounds/*.{png,jpg} 2>/dev/null | head -2)
fi
for ws in 1 2; do
  target hyprctl dispatch "hl.dsp.focus({ workspace = \"$ws\" })" >/dev/null
  target hyprctl dispatch "hl.dsp.exec_cmd(\"imv -f -s full ${pics[ws - 1]}\")" >/dev/null
  sleep 2
done

for _ in $(seq "$rounds"); do
  target hyprctl dispatch 'hl.dsp.focus({ workspace = "1" })' >/dev/null
  sleep 0.1
  target hyprctl dispatch 'hl.dsp.focus({ workspace = "2" })' >/dev/null
  sleep 0.1
done
# End on the first picture (a real wallpaper): what is still wrong now stays wrong
target hyprctl dispatch 'hl.dsp.focus({ workspace = "1" })' >/dev/null
sleep 4

WAYLAND_DISPLAY=$(<"$stage/console-target-$uid/wayland") grim -t ppm -o HEADLESS-RDP "$out/server.ppm"
WAYLAND_DISPLAY=$(<"/tmp/omxrdp-client-$uid/wayland") grim -t ppm "$out/client.ppm"
python3 "$stage/tests/compare-ppm.py" "$out/server.ppm" "$out/client.ppm"
rc=$?

"$client" stop
"$stage/stop" 20 2>/dev/null
target stop
exit $rc
