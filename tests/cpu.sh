#!/bin/bash
# Dev helper: CPU used by a test session's pieces (the test user's
# Hyprland/wayvnc/proxy/cage) and by the test xrdp and client, over SECONDS
# (default 10), from /proc tick counters. Usage: cpu.sh TEST_USER [SECONDS]
user=$1
secs=${2:-10}
ticks=$(getconf CLK_TCK)

pids() {
  pgrep -u "$user" -f 'omarchy-xrdp-vnc-proxy|omarchy-xrdp-backend|^wayvnc|^Hyprland|omarchy-xrdp/cage|^foot'
  pgrep -u "$(id -u)" -f '^xrdp --nodaemon --config|^xfreerdp3'
}

snapshot() {
  local pid stat
  for pid in $(pids); do
    stat=$(<"/proc/$pid/stat") 2>/dev/null || continue
    stat=${stat##*) }
    read -r -a f <<<"$stat"
    echo "$pid $((f[11] + f[12])) $(tr '\0' ' ' <"/proc/$pid/cmdline" | cut -c1-60)"
  done
}

declare -A before
while read -r pid t _; do before[$pid]=$t; done < <(snapshot)
sleep "$secs"
while read -r pid t cmd; do
  [[ -n ${before[$pid]:-} ]] || continue
  awk -v d="$((t - before[$pid]))" -v hz="$ticks" -v s="$secs" -v c="$cmd" \
    'BEGIN { printf "%5.1f%%  %s\n", d * 100 / hz / s, c }'
done < <(snapshot) | sort -rn
