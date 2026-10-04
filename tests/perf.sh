#!/bin/bash
# Performance snapshot of an RDP test session under load (run as the
# developer; the test client must be connected to the private xrdp on PORT).
# Reports bytes/s from xrdp to the client, distinct client frames/s sampled
# from the client's screen, and CPU per component.
# Usage: perf.sh TEST_USER [PORT] [SECONDS]
set -u
user=$1 port=${2:-13389} secs=${3:-10}
stage=${STAGE:-/tmp/omxrdp-test}
client=$stage/tests/rdp-client.sh
as_user() { sudo -n -u "$user" "$@"; }

# Full-screen scrolling terminal
as_user "$stage/tests/launch.sh" 20 "foot $stage/tests/load.sh" >/dev/null
sleep 2
as_user "$stage/tests/hypr.sh" 20 dispatch 'hl.dsp.window.fullscreen()' >/dev/null
sleep 2

# Bytes the client has received on its connection to the private xrdp
rx() { ss -tniH "dport = :$port" | grep -oE 'bytes_received:[0-9]+' | cut -d: -f2 | awk '{s+=$1} END {print s+0}'; }

"$stage/tests/cpu.sh" "$user" "$secs" >"$stage/perf-cpu.txt" &
cpu_pid=$!
rx0=$(rx)
t0=$(date +%s.%N)

# Sample the client screen ~10 times a second and count distinct images
frames=0 last=""
end=$((SECONDS + secs))
while ((SECONDS < end)); do
  sum=$("$client" shot - 2>/dev/null | md5sum | cut -c1-16)
  [[ $sum != "$last" ]] && ((frames++))
  last=$sum
  sleep 0.1
done
t1=$(date +%s.%N)
rx1=$(rx)
wait "$cpu_pid"

awk -v b="$((rx1 - rx0))" -v t0="$t0" -v t1="$t1" -v f="$frames" 'BEGIN {
  d = t1 - t0
  printf "network: %.1f Mbit/s from xrdp to the client\n", b * 8 / d / 1e6
  printf "client:  %.1f distinct frames/s seen (sampling caps this near 10)\n", f / d
}'
echo "cpu (% of one core):"
cat "$stage/perf-cpu.txt"
