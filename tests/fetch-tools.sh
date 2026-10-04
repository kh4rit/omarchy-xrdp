#!/bin/bash
# Dev helper: unpack the test client's tools (xdotool, wlr-randr) from the
# Arch repos into $STAGE/tools without installing them system-wide.
set -eu
stage=${STAGE:-/tmp/omxrdp-test}
mkdir -p "$stage/tools" "$stage/pkgs"
cd "$stage/pkgs"
for pkg in xdotool wlr-randr xclip; do
  url=$(pacman -Sp --print-format '%l' "$pkg" | tail -1)
  file=${url##*/}
  [[ -f $file ]] || curl -sfLO "$url"
  bsdtar -xf "$file" -C "$stage/tools" --exclude '.*'
done
ls "$stage/tools/usr/bin"
