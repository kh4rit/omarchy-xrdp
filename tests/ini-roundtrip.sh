#!/bin/bash
# Check that `omarchy-xrdp setup` edits copies of the installed xrdp.ini and
# sesman.ini as intended, is idempotent, and that `remove` restores them
# byte for byte.
set -eu
root=$(dirname "$(readlink -f "$0")")/..
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

cp /etc/xrdp/xrdp.ini /etc/xrdp/sesman.ini "$tmp/"
cp "$tmp/xrdp.ini" "$tmp/xrdp.ini.orig"
cp "$tmp/sesman.ini" "$tmp/sesman.ini.orig"

run() {
  OMARCHY_XRDP_NO_ROOT=1 XRDP_INI="$tmp/xrdp.ini" SESMAN_INI="$tmp/sesman.ini" \
    "$root/bin/omarchy-xrdp" "$@"
}

run setup
echo "--- xrdp.ini changes"
diff "$tmp/xrdp.ini.orig" "$tmp/xrdp.ini" || true
echo "--- sesman.ini changes"
diff "$tmp/sesman.ini.orig" "$tmp/sesman.ini" || true

cp "$tmp/xrdp.ini" "$tmp/xrdp.ini.once"
cp "$tmp/sesman.ini" "$tmp/sesman.ini.once"
run setup
cmp "$tmp/xrdp.ini" "$tmp/xrdp.ini.once" && cmp "$tmp/sesman.ini" "$tmp/sesman.ini.once"
echo "--- second setup changed nothing"

run remove
cmp "$tmp/xrdp.ini" "$tmp/xrdp.ini.orig" && cmp "$tmp/sesman.ini" "$tmp/sesman.ini.orig"
echo "--- remove restored both files exactly"
