#!/bin/bash
# Run as the test user: show how the given systemd --user units are bound to
# the session, and what is active; --stop stops them first.
# Usage: user-units.sh [--stop] UNIT...
export XDG_RUNTIME_DIR=/run/user/$(id -u)
if [[ ${1:-} == --stop ]]; then
  shift
  systemctl --user stop "$@"
fi
for unit in "$@"; do
  echo "== $unit: $(systemctl --user is-active "$unit")"
  systemctl --user cat "$unit" 2>/dev/null | grep -E '^(PartOf|BindsTo|After|Requisite|WantedBy|Wants|ExecStart)='
done
echo "== graphical-session.target: $(systemctl --user is-active graphical-session.target)"
