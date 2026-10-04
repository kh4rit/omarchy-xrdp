# Changelog

## 0.5.2 — first public release (alpha)

Tested on one machine (Arch/Omarchy, Hyprland 0.56.2, xrdp 0.10.6, Intel
graphics) with Windows App on macOS, Windows Remote Desktop and FreeRDP.
Expect rough edges.

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
