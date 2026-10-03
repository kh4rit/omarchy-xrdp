#!/bin/bash
# Dev helper: build the package without installing it or checking
# dependencies, using a private meson/ninja and wlroots0.20 when the system
# doesn't have them yet. Output goes to $OUT (default /tmp/omxrdp-mk).
# Usage: tests/build-pkg.sh [extra makepkg args]
set -eu
root=$(dirname "$(readlink -f "$0")")/..
out=${OUT:-/tmp/omxrdp-mk}
mkdir -p "$out"
[[ -n ${DEV_TOOLS:-} ]] && export PATH="$DEV_TOOLS:$PATH"
[[ -n ${DEV_PKG_CONFIG_PATH:-} ]] && export PKG_CONFIG_PATH=$DEV_PKG_CONFIG_PATH
cd "$root"
BUILDDIR=$out SRCDEST=$out PKGDEST=$out makepkg -d -f "$@"
