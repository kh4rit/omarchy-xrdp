#!/bin/bash
# Check `omarchy-xrdp setup` and `remove` on copies of the installed
# xrdp.ini and sesman.ini (which may already carry an earlier version of the
# setup): setup must upgrade cleanly, be idempotent, and remove must restore
# the pristine files (setup's first backups, or the current files if there
# are none) byte for byte.
set -eu
root=$(dirname "$(readlink -f "$0")")/..
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

for f in xrdp.ini sesman.ini; do
  cp "/etc/xrdp/$f" "$tmp/$f"
  if [[ -r /etc/xrdp/$f.omarchy-xrdp.bak ]]; then
    cp "/etc/xrdp/$f.omarchy-xrdp.bak" "$tmp/$f.pristine"
  else
    cp "/etc/xrdp/$f" "$tmp/$f.pristine"
  fi
done

run() {
  OMARCHY_XRDP_NO_ROOT=1 XRDP_INI="$tmp/xrdp.ini" SESMAN_INI="$tmp/sesman.ini" \
    "$root/bin/omarchy-xrdp" "$@"
}

run setup
echo "--- xrdp.ini changes from pristine"
diff "$tmp/xrdp.ini.pristine" "$tmp/xrdp.ini" || true
echo "--- sesman.ini changes from pristine"
diff "$tmp/sesman.ini.pristine" "$tmp/sesman.ini" || true

cp "$tmp/xrdp.ini" "$tmp/xrdp.ini.once"
cp "$tmp/sesman.ini" "$tmp/sesman.ini.once"
run setup
cmp "$tmp/xrdp.ini" "$tmp/xrdp.ini.once" && cmp "$tmp/sesman.ini" "$tmp/sesman.ini.once"
echo "--- second setup changed nothing"

run remove
cmp "$tmp/xrdp.ini" "$tmp/xrdp.ini.pristine" && cmp "$tmp/sesman.ini" "$tmp/sesman.ini.pristine"
echo "--- remove restored the pristine files exactly"
