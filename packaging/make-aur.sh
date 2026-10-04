#!/bin/bash
# Generate the AUR package (PKGBUILD + .SRCINFO) from this repo's PKGBUILD,
# for the release tag v$pkgver that is already pushed to GitHub.
#
# Usage: packaging/make-aur.sh GITHUB_USER [OUTDIR [--dry-run]]   (default OUTDIR: packaging/aur)
#
# The repo's PKGBUILD builds from the checkout ($startdir); the AUR one
# downloads the tagged source tarball instead. Then publish:
#   git clone ssh://aur@aur.archlinux.org/omarchy-xrdp.git
#   cp packaging/aur/{PKGBUILD,.SRCINFO} omarchy-xrdp/ && cd omarchy-xrdp
#   git add PKGBUILD .SRCINFO && git commit -m "Update to $pkgver" && git push
set -euo pipefail

owner=${1:?Usage: make-aur.sh GITHUB_USER [OUTDIR]}
root=$(dirname "$(readlink -f "$0")")/..
out=${2:-$root/packaging/aur}
mkdir -p "$out"

src='$srcdir/$pkgname-$pkgver'
sed -e "1s|.*|# Maintainer: $(git -C "$root" log -1 --format='%an <%ae>')|" \
  -e "s|GITHUB_USER|$owner|g" \
  -e "s|\\\$startdir|$src|g" \
  -e 's|^source=(\(.*\))$|source=("$pkgname-$pkgver.tar.gz::$url/archive/refs/tags/v$pkgver.tar.gz"\n        \1)|' \
  -e "s|^sha256sums=('\(.*\)')$|sha256sums=('SKIP'\n            '\1')|" \
  "$root/PKGBUILD" >"$out/PKGBUILD"

cd "$out"
if [[ ${3:-} == --dry-run ]]; then
  echo "PKGBUILD in $out (dry run: no checksum, no .SRCINFO)"
  exit 0
fi
# The release tarball's checksum (fails if the tag isn't on GitHub yet)
updpkgsums
makepkg --printsrcinfo >.SRCINFO
echo "AUR package in $out"
