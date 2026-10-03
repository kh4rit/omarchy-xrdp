#!/bin/bash
# Run as the test user: install (or with --remove, remove) the staged
# omarchy-xrdp-session.target as a per-user unit, standing in for the
# package's /usr/lib/systemd/user copy. Usage: user-unit.sh [--remove]
export XDG_RUNTIME_DIR=/run/user/$(id -u)
stage=${STAGE:-/tmp/omxrdp-test}
dest=${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user
if [[ ${1:-} == --remove ]]; then
  rm -f "$dest/omarchy-xrdp-session.target"
else
  mkdir -p "$dest"
  cp "$stage/systemd/omarchy-xrdp-session.target" "$dest/"
fi
systemctl --user daemon-reload
systemctl --user cat omarchy-xrdp-session.target >/dev/null 2>&1 && echo "unit installed" || echo "unit not installed"
