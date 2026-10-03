# Local package for this checkout: makepkg -si (or ./install.sh)

pkgname=omarchy-xrdp
pkgver=0.1.0
pkgrel=1
_cagever=0.3.1
pkgdesc="Remote Omarchy (Hyprland) desktops over RDP, through xrdp"
arch=(x86_64)
url="https://github.com/cage-kiosk/cage"
license=(MIT)
depends=(xrdp hyprland wayvnc wlroots0.20 xorg-xwayland xorg-xauth jq procps-ng bash python)
makedepends=(meson ninja wayland-protocols)
optdepends=('pipewire-module-xrdp: sound in RDP sessions')
options=(!debug)
source=("cage-$_cagever.tar.gz::https://github.com/cage-kiosk/cage/archive/refs/tags/v$_cagever.tar.gz")
sha256sums=('6dc1619665acd367e0174c93b234002549a66f55f1de9197d67f0305415babc8')

prepare() {
  patch -d "cage-$_cagever" -p1 <"$startdir/cage/xdg-shell-v6.patch"
}

build() {
  meson setup --prefix=/usr --buildtype=plain -Dman-pages=disabled build "cage-$_cagever"
  meson compile -C build
}

package() {
  local lib="$pkgdir/usr/lib/omarchy-xrdp"

  install -Dm755 build/cage "$lib/cage"
  install -Dm644 "cage-$_cagever/LICENSE" "$pkgdir/usr/share/licenses/$pkgname/LICENSE.cage"

  for script in omarchy-xrdp-session omarchy-xrdp-compositor omarchy-xrdp-startwm omarchy-xrdp-wm \
    omarchy-xrdp-resize omarchy-xrdp-vnc-proxy; do
    install -Dm755 "$startdir/bin/$script" "$lib/$script"
  done
  install -Dm755 "$startdir/shims/uwsm" "$lib/shims/uwsm"
  install -Dm644 "$startdir/systemd/omarchy-xrdp-session.target" \
    "$pkgdir/usr/lib/systemd/user/omarchy-xrdp-session.target"
  install -Dm755 "$startdir/bin/omarchy-xrdp" "$pkgdir/usr/bin/omarchy-xrdp"
  install -Dm644 "$startdir/README.md" "$pkgdir/usr/share/doc/$pkgname/README.md"
}
