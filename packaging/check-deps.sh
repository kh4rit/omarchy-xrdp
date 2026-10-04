#!/bin/bash
# Packaging check for a built omarchy-xrdp package: the libraries its
# binaries link against and the commands its scripts run, each mapped to
# the package that provides it, to compare with depends= in the PKGBUILD
# (Arch package guidelines: list all direct dependencies, don't rely on
# transitive ones).
# Usage: packaging/check-deps.sh omarchy-xrdp-*.pkg.tar.zst
set -eu
pkg=$(readlink -f "$1")
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
tar -xf "$pkg" -C "$tmp"

owner() { pacman -Qoq "$1" 2>/dev/null | head -1 || echo "?"; }

echo "== Libraries"
for bin in $(find "$tmp/usr" -type f -perm -u+x); do
  file -b "$bin" | grep -q ELF || continue
  for lib in $(readelf -d "$bin" | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p'); do
    path=$(ldconfig -p | awk -v l="$lib" '$1 == l { print $NF; exit }')
    echo "$lib $(owner "$path")"
  done
done | sort -u | awk '{ printf "  %-28s %s\n", $1, $2 }'

echo "== Commands run by the scripts"
for script in $(find "$tmp/usr" -type f -perm -u+x); do
  file -b "$script" | grep -q 'shell script' || continue
  grep -hoE '(^|[;&|(]|\$\(|then|do|exec|&&|\|\|)[[:space:]]*[a-zA-Z][a-zA-Z0-9_.+-]*' "$script" |
    grep -oE '[a-zA-Z][a-zA-Z0-9_.+-]*$'
done | sort -u | while read -r cmd; do
  path=$(command -v "$cmd" 2>/dev/null) || continue
  [[ $path == /* ]] || continue # builtins and keywords
  echo "  $cmd $(owner "$path")"
done | awk '{ printf "  %-22s %s\n", $1, $2 }'
