#!/bin/bash
# Run as the test user: start an emulated xrdp session as a transient
# systemd --user service, so it lives in that user's own manager and not in
# the caller's login session (no polkit prompts leak to the caller's screen).
# Usage: start-test-session.sh DISPLAY [WxH]
set -eu
display=$1 geometry=${2:-1600x900}
export XDG_RUNTIME_DIR=/run/user/$(id -u)
here=$(dirname "$(readlink -f "$0")")

# A second session of the same user would take over its systemd --user
# environment (one session per user), so never test next to a real one
for dir in "$XDG_RUNTIME_DIR"/omarchy-xrdp/*/; do
  [[ -n ${OMARCHY_XRDP_ATTACH:-} ]] && break  # console tests attach on purpose
  d=$(basename "$dir")
  [[ $d == "$display" || ! -f $dir/pid ]] && continue
  if kill -0 "$(<"$dir/pid")" 2>/dev/null; then
    echo "$(id -un) has a live Omarchy RDP session on :$d; not starting a test session next to it" >&2
    exit 1
  fi
done
systemctl --user stop "omxrdp-test-$display" 2>/dev/null || true
systemd-run --user --quiet --collect --unit="omxrdp-test-$display" \
  --working-directory="$HOME" \
  ${LD_LIBRARY_PATH:+--setenv=LD_LIBRARY_PATH=$LD_LIBRARY_PATH} \
  $(compgen -e | grep '^OMARCHY_XRDP_' | sed 's/^/--setenv=/') \
  "$here/sesman-emulate.sh" "$display" "${SOCKET_DIR:-/tmp/omxrdp-test}/xup-$display.sock" "$geometry"
for _ in $(seq 100); do
  [[ -f $XDG_RUNTIME_DIR/omarchy-xrdp/$display/pid ]] && echo "session :$display ready" && exit 0
  systemctl --user is-active --quiet "omxrdp-test-$display" || break
  sleep 0.2
done
echo "session :$display failed to start"
journalctl --user -u "omxrdp-test-$display" --no-pager -n 20
exit 1
