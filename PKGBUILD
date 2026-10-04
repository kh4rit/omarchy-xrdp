# Local package for this checkout: makepkg -si (or ./install.sh)

pkgname=omarchy-xrdp
pkgver=0.5.2
pkgrel=1
_cagever=0.3.1
pkgdesc="Remote Omarchy (Hyprland) desktops over RDP, through xrdp"
arch=(x86_64)
url="https://github.com/GITHUB_USER/omarchy-xrdp"
license=(MIT)
depends=(xrdp hyprland wlroots0.20 wayland libxkbcommon libxcb libxcursor xorg-xwayland xorg-xauth jq procps-ng bash)
makedepends=(meson ninja wayland-protocols python)
optdepends=('pipewire-module-xrdp: sound in RDP sessions')
options=(!debug)
source=("cage-$_cagever.tar.gz::https://github.com/cage-kiosk/cage/archive/refs/tags/v$_cagever.tar.gz")
sha256sums=('6dc1619665acd367e0174c93b234002549a66f55f1de9197d67f0305415babc8')

prepare() {
  patch -d "cage-$_cagever" -p1 <"$startdir/cage/xdg-shell-v6.patch"
}

build() {
  meson setup --prefix=/usr --buildtype=plain -Dman-pages=disabled build-cage "cage-$_cagever"
  meson compile -C build-cage
  # xrdp's headers (xrdp_client_info.h) come with the xrdp package
  meson setup --prefix=/usr --buildtype=plain build-backend "$startdir/backend"
  meson compile -C build-backend
  python "$startdir/cursor-theme/generate.py" hidden-cursors
}

package() {
  local lib="$pkgdir/usr/lib/omarchy-xrdp"

  install -Dm755 build-cage/cage "$lib/cage"
  install -Dm644 "$startdir/LICENSE" "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
  install -Dm644 "cage-$_cagever/LICENSE" "$pkgdir/usr/share/licenses/$pkgname/LICENSE.cage"
  install -Dm755 build-backend/omarchy-xrdp-backend "$lib/omarchy-xrdp-backend"

  for script in omarchy-xrdp-xserver omarchy-xrdp-session omarchy-xrdp-compositor \
    omarchy-xrdp-startwm omarchy-xrdp-wm omarchy-xrdp-resize omarchy-xrdp-console omarchy-xrdp-cursor; do
    install -Dm755 "$startdir/bin/$script" "$lib/$script"
  done
  install -Dm755 "$startdir/shims/uwsm" "$lib/shims/uwsm"
  install -d "$pkgdir/usr/share/icons"
  cp -r hidden-cursors "$pkgdir/usr/share/icons/omarchy-xrdp-hidden"
  install -Dm644 "$startdir/systemd/omarchy-xrdp-session.target" \
    "$pkgdir/usr/lib/systemd/user/omarchy-xrdp-session.target"
  install -Dm755 "$startdir/bin/omarchy-xrdp" "$pkgdir/usr/bin/omarchy-xrdp"
  install -Dm644 "$startdir/README.md" "$pkgdir/usr/share/doc/$pkgname/README.md"
}
