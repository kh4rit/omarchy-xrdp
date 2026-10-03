# omarchy-xrdp

Remote **Omarchy** desktops over RDP. A user connects with any RDP client
(Windows Remote Desktop, Remmina, FreeRDP), logs in through xrdp, and gets
their own Hyprland session with their Omarchy config: bar, theme, keybindings,
tiling. It runs alongside whoever is using the machine's own screens, with no
shared input or display.

## How it works

xrdp can hand a session to a VNC server over a Unix socket (session `code=1`).
omarchy-xrdp registers itself as that server:

```
RDP client ──► xrdp ──(Unix socket)──► vnc-proxy ──► wayvnc ──► Hyprland (user's Omarchy config)
                │                                                ▲ renders on the GPU into a
                └─ xrdp-sesman starts, as the user:              │ virtual HEADLESS-RDP output
                     omarchy-xrdp-session ───────────────────────┘
                       cage (headless)  gives Hyprland a GPU render node
                       start-hyprland   with ~/.config/hypr + the RDP output
                       wayvnc           captures that output, injects input
                       vnc-proxy        serves xrdp's socket, handles resizing
                       Xwayland :N      the X display sesman/chansrv wait for
                     omarchy-xrdp-wm    sound, and lives as long as the session
```

Hyprland can't start without a DRM seat or a parent Wayland compositor, and an
RDP session has neither, so a headless `cage` acts as the parent. cage 0.3.1
offers xdg-shell v5 while Hyprland's nested backend needs v6; the package
builds cage with a one-line patch (`cage/xdg-shell-v6.patch`).

Each session runs entirely as the logged-in user. Keyboard and mouse go through
wayvnc's virtual input devices into that user's Hyprland only.

A local Omarchy login is run by uwsm, which brings up `graphical-session.target`
(and with it Omarchy's session services: input method, crash watch, ...) and
clears the session out of `systemd --user` at logout. In an RDP session
`omarchy-xrdp-session.target` does that job, and a small `uwsm` shim turns
Omarchy's logout (`uwsm stop`) into ending the RDP session.

`omarchy-xrdp-vnc-proxy` exists for resizing. xrdp only resizes a VNC session
if the server's first update describes the screen layout, which wayvnc doesn't
do, and wayvnc's own resize path (wlr-output-management) proved unreliable
with Hyprland. The proxy supplies the initial layout, turns xrdp's resize
requests into Hyprland monitor rules (`omarchy-xrdp-resize`), and reports the
result back the way xrdp expects. Everything else passes through unchanged.

## Install

Requires xrdp and xorgxrdp (AUR) already installed and running.

```
./install.sh
```

This installs the dependencies with `omarchy pkg add`, builds and installs the
`omarchy-xrdp` package with `makepkg -si`, and runs `sudo omarchy-xrdp setup`.

`omarchy-xrdp setup` makes three marked, reversible edits:

| File | Change |
|---|---|
| `/etc/xrdp/xrdp.ini` | Adds an `[Omarchy]` session before `[Xorg]`, so it is the login screen default |
| `/etc/xrdp/sesman.ini` | `[Xvnc] param=` → `/usr/lib/omarchy-xrdp/omarchy-xrdp-session` |
| `/etc/xrdp/sesman.ini` | `DefaultWindowManager=` → `/usr/lib/omarchy-xrdp/omarchy-xrdp-startwm` (falls back to `startwm.sh` for other session types, so Xorg/XFCE keeps working) |

Running sessions aren't affected: setup reloads xrdp-sesman instead of
restarting it.

Users need Omarchy's config in their home (new accounts get it from
`/etc/skel`) and, if sesman restricts logins to a group, membership in it
(`TerminalServerUsers`, e.g. `tsusers`).

## Use

Connect to port 3389, pick **Omarchy** (the default) and log in.

- The desktop follows the RDP window: its size at login, when the window is
  resized (with the client's dynamic resolution, the default in Windows Remote
  Desktop), and when reconnecting from a different screen.
- Omarchy's **Logout** ends the RDP session. Just closing the RDP window leaves
  the session running, and the next login reconnects to it.
- The Windows key reaches Omarchy when Remote Desktop is full screen (or with
  "Apply Windows key combinations: On the remote computer").
- After the idle timeout Omarchy locks the session; unlock with the user's
  password.

Per-user settings go in `~/.config/omarchy/xrdp.conf`:

```bash
SCALE=auto          # auto: 1 below 2560 px wide, 1.5 below 3200, else 2
KEYBOARD=us         # xkb layout (and -variant) for wayvnc's virtual keyboard
MAX_FPS=60
RENDER_NODE=/dev/dri/renderD128
RESIZE=yes          # no: skip the proxy, the desktop keeps its login size
WAYVNC_LOG=warning  # wayvnc --log-level, e.g. debug
PROXY_DEBUG=        # 1: log the proxy's resize handling
```

Logs: `~/.local/state/omarchy-xrdp/session-<display>.log` for each session,
plus the usual `/var/log/xrdp*.log`. `omarchy-xrdp status` shows the setup
and running sessions.

## Limitations

- **Single monitor.** The session is one screen; multi-monitor RDP setups
  are untested.
- **Clipboard** goes through xrdp's VNC clipboard, which is Latin-1 text only.
- **Sound** uses pipewire-module-xrdp, which is per user: two simultaneous
  sessions of the same user share (and fight over) one sink.
- **One RDP session per user** is the sensible setup; Omarchy's systemd
  `--user` environment is shared between a user's sessions.
- Takes over sesman's `[Xvnc]` session type, so TigerVNC-backed Xvnc sessions
  are no longer available.
- XDG autostart entries (`~/.config/autostart`, `/etc/xdg/autostart`) don't
  run; Omarchy's own autostart (in the Hyprland config) does.

## Uninstall

```
sudo omarchy-xrdp remove
sudo pacman -R omarchy-xrdp
```

## Development

`tests/` runs everything without root:

- `tests/stage.sh` copies the scripts to `/tmp/omxrdp-test`;
  `start-test-session.sh` launches a session the way xrdp-sesman does
  (`sesman-emulate.sh`) as a transient `systemd --user` unit of the test user.
- `tests/rdp-client.sh` runs FreeRDP inside a private headless cage, driven
  with xdotool and captured with grim, against an unprivileged xrdp instance.
  Use `RDP_CLIENT=xfreerdp3` (the SDL client draws nothing in a headless
  cage). The first input event after connecting only gives the X11 client
  focus, so click or send a throwaway key before typing.
- `tests/vnc-probe.py` / `tests/vnc-input.py` talk RFB to the session socket
  directly.
- `tests/resize-scenario.sh` repeatedly connects at a new size and checks the
  session follows.
- `tests/ini-roundtrip.sh` checks `setup` and `remove` against copies of the
  installed ini files.
