# Changelog

## 0.7.1

Packaging only, following the Arch package and AUR submission guidelines:

- Complete runtime dependencies (coreutils, gawk, glibc, grep, sed, systemd);
  the unused xorg-xauth dropped.
- An install script prints the setup steps after installing or upgrading.
- Scripts use `/usr/bin/bash` and `/usr/bin/sh` (namcap).
- `install.sh` enables xrdp and installs the right build dependencies.

## 0.7.0 — first public release (alpha)

Tested on one machine (Arch/Omarchy, Hyprland 0.56.2, xrdp 0.10.6, Intel
graphics) with Windows App on macOS, Windows Remote Desktop and FreeRDP.
Expect rough edges. The features are summed up under 0.5.2; changes since:

- 60 frames/s with the proven pipeline is the default again. `FRAME_RATE`
  above 60 turns on the faster pipeline from 0.6.0 as an **experimental**
  feature, with known issues in Windows App on macOS (see README).
- `omarchy-xrdp setup` no longer changes xrdp's x264 settings, and undoes
  0.6.0's `threads = 4`, which made Windows App on macOS show corrupted
  blocks after big screen changes.
- Kept: xrdp's raised H.264 frame limit (0.6.1) and the frame statistics
  in the session log (0.6.2).

## 0.6.1

- Fix green, smeared blocks after big screen changes (e.g. fast workspace
  switches at 4K): xrdp silently drops any H.264 frame over 3 MB, and the
  client then decodes everything after it against the wrong picture. The
  package raises the limit to 32 MB for xrdp.service
  (`XRDP_GFX_MAX_COMPRESSED_BYTES`). Reproduced with
  `tests/artifact-test.sh` through `tests/throttle-proxy.py` (150 Mbit/s).

## 0.6.0

- `FRAME_RATE` per user (e.g. 120 on a LAN): frames/s sent and the RDP
  monitor's refresh rate.
- Faster frame pipeline: the next frame is captured while xrdp encodes the
  last one (1080p worst case: 30 → 46 frames/s at the default 60, ~70 at
  120), and big updates are converted on several cores (4K: 13 → 2.6 ms).
- `omarchy-xrdp setup` sets x264 to 4 threads per screen in gfx.toml
  (restored by `remove`).

## 0.5.2

The feature set:

- Native xrdp backend (`omarchy-xrdp-backend`, speaks xrdp's xup protocol):
  H.264/RemoteFX through the RDP graphics pipeline, damage tracking,
  keyboard with the client's layout, smooth scrolling, dynamic resolution,
  text clipboard.
- Each RDP user gets their own Omarchy (Hyprland) session, with the usual
  Omarchy session services; Omarchy's Logout ends it.
- Console mode: an RDP login as a user who is logged in at the machine
  attaches to that desktop instead (virtual monitor sized to the client;
  the machine's monitors go dark; local input ends the remote session and
  locks the desktop). `CONSOLE=no` opts out.
- The client draws the cursor (no lag), with the right shape and size.
- `MAC_SHORTCUTS=yes`: when Windows App on macOS translates Command+C/X/V
  to Ctrl, they become Omarchy's copy/cut/paste again (terminals
  included). Simpler: uncheck those shortcuts in Windows App → Settings →
  Keyboard.
- `omarchy-xrdp setup` turns off xrdp's `XorgNoNewPrivileges` for these
  sessions, so sudo and polkit work inside them.
