#!/usr/bin/bash

# Install omarchy-xrdp: build and install the package, then add the Omarchy
# session to xrdp. Run as your normal user; it asks for sudo when needed.
# Expects xrdp to be installed already (AUR).

set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")"

if ((EUID == 0)); then
  echo "Run as your normal user, not root (makepkg refuses root)." >&2
  exit 1
fi

if ! omarchy pkg present xrdp; then
  echo "xrdp isn't installed. Install it first, e.g.:" >&2
  echo "  yay -S --needed --mflags --nocheck xrdp xorgxrdp" >&2
  exit 1
fi

echo "==> Installing dependencies"
omarchy pkg add meson ninja python wayland-protocols wlroots0.20 libxkbcommon libxcb libxcursor xorg-xwayland jq

echo "==> Building and installing omarchy-xrdp"
makepkg -sif --noconfirm

echo "==> Adding the Omarchy session to xrdp"
sudo omarchy-xrdp setup
sudo systemctl daemon-reload
sudo systemctl enable --now xrdp

echo
omarchy-xrdp status
echo
echo "Done. Connect with Remote Desktop and pick the \"Omarchy\" session (the default)."
echo "Only members of the tsusers group can log in over RDP: sudo usermod -aG tsusers USER"
echo "If xrdp was already running, apply the upgrade with: sudo systemctl restart xrdp"
echo "(this disconnects RDP clients; their sessions keep running)."
