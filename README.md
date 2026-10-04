# omarchy-xrdp

Remote **Omarchy** desktops over RDP. A user connects with any RDP client
(Windows Remote Desktop, Windows App on macOS, Remmina, FreeRDP), logs in
through xrdp, and gets their own Hyprland session with their Omarchy config:
bar, theme, keybindings, tiling. It runs alongside whoever is using the
machine's own screens, with no shared input or display, and is encoded the
way xrdp encodes its own Xorg sessions: H.264 (or RemoteFX) through the RDP
graphics pipeline.

## How it works

xrdp's own Xorg sessions use a backend, xorgxrdp, that hands frames to xrdp
over a Unix socket (the "xup" protocol); xrdp then encodes them with its GFX
pipeline. omarchy-xrdp provides a backend that speaks the same protocol for a
Hyprland session:

```
RDP client ◄── H.264/RFX ── xrdp (xup module) ◄── shared memory frames ── omarchy-xrdp-backend
                              │   └─ chansrv: clipboard, sound            │  captures ▲   │ injects input
                              │                                           │           │   ▼
                              └─ xrdp-sesman starts, as the user:          Hyprland (user's Omarchy config)
                                   omarchy-xrdp-xserver → omarchy-xrdp-session
                                     cage (headless)   gives Hyprland a GPU render node
                                     start-hyprland    ~/.config/hypr + a virtual HEADLESS-RDP output
                                     omarchy-xrdp-backend
                                     Xwayland :N       the X display sesman/chansrv wait for
                                   omarchy-xrdp-wm     sound, and lives as long as the session
```

`omarchy-xrdp-backend` (C):

- captures the output with `ext-image-copy-capture` into memory xrdp maps
  directly, converting only what changed to NV12 (H.264) or RemoteFX tiles;
- keeps one frame in flight and captures the next once xrdp has encoded it,
  so an idle desktop costs nothing and a busy one runs at the client's pace;
- has the client draw the cursor locally, so it moves without lag (as with
  a Windows host). Hyprland 0.56 can't hand out its cursor images and draws
  the cursor into the video on a virtual monitor, so the session switches
  Hyprland to `omarchy-xrdp-hidden`, an invisible cursor theme whose shapes
  differ only in their hotspots. The backend reads the hotspot, works out
  the shape and sends the client the real image from the user's cursor
  theme at the session's scale. (Hyprland's copying of cursor themes into
  GTK settings is switched off first, so those never change.) Cursors an
  app draws itself, which are rare, stay in the video;
- injects keyboard (RDP scancodes, with the client's keyboard layout) and
  mouse (including smooth scrolling) through virtual input devices;
- follows client resizes (dynamic resolution) by resizing the Hyprland output;
- bridges the text clipboard between Hyprland and the X display where xrdp's
  chansrv serves the RDP clipboard.

Hyprland can't start without a DRM seat or a parent Wayland compositor, and
an RDP session has neither, so a headless `cage` acts as the parent. cage
0.3.1 offers xdg-shell v5 while Hyprland's nested backend needs v6; the
package builds cage with a one-line patch (`cage/xdg-shell-v6.patch`).

A local Omarchy login is run by uwsm, which brings up `graphical-session.target`
(and with it Omarchy's session services) and clears the session out of
`systemd --user` at logout. In an RDP session `omarchy-xrdp-session.target`
does that job, and a small `uwsm` shim turns Omarchy's logout (`uwsm stop`)
into ending the RDP session.

## Install

Requires xrdp and xorgxrdp (AUR) installed and running.

```
./install.sh
```

This installs the dependencies with `omarchy pkg add`, builds and installs
the `omarchy-xrdp` package with `makepkg -si`, and runs
`sudo omarchy-xrdp setup`, which makes these marked, reversible edits (and
upgrades an earlier omarchy-xrdp setup):

| File | Change |
|---|---|
| `/etc/xrdp/xrdp.ini` | Adds an `[Omarchy]` session (`lib=libxup.so`, `code=20`) as the login screen default; comments out the `[Xorg]` entry, which would now lead to the same place |
| `/etc/xrdp/sesman.ini` | `[Xorg] param=` → `omarchy-xrdp-xserver`: Omarchy for users with an Omarchy config, stock Xorg otherwise |
| `/etc/xrdp/sesman.ini` | `DefaultWindowManager=` → `omarchy-xrdp-startwm` (falls back to `startwm.sh`, so Xorg/XFCE sessions keep working) |

Running sessions aren't affected: setup reloads xrdp-sesman instead of
restarting it. `sudo omarchy-xrdp remove` puts the files back exactly.

Users need Omarchy's config in their home (new accounts get it from
`/etc/skel`) and, if sesman restricts logins to a group, membership in it
(`TerminalServerUsers`, e.g. `tsusers`).

## Use

Connect to port 3389, pick **Omarchy** (the default) and log in.

- The desktop follows the RDP window: its size at login, when the window is
  resized (dynamic resolution), and when reconnecting from another screen.
- Omarchy's **Logout** ends the RDP session. Closing the RDP window leaves it
  running; the next login reconnects to it.
- **Super** shortcuts and Super+drag need the client to send the Windows key.
  Windows Remote Desktop does when full screen (or with "Apply Windows key
  combinations: On the remote computer"). Windows App on macOS sends
  Command as the Windows key, except that it always turns Command+C, X, V,
  A, Z, F and W into Ctrl shortcuts (there is no setting); set
  `MAC_SHORTCUTS=yes` (below) so Command+C/X/V copy and paste in terminals
  too. It also never holds Command during a mouse drag, so Command+drag
  can't move windows from Windows App.
- After the idle timeout Omarchy locks the session; unlock with the user's
  password.

### Your own desktop (console mode)

If you're logged in at the machine itself, an RDP login as the same user
attaches to that running desktop instead of starting a second one, like
connecting to a Windows PC's console session:

- A virtual monitor sized to the RDP window is added and the current
  workspace moves onto it; every other window and workspace is still there.
- The machine's own monitors go dark while the desktop is in use remotely.
- Touching the machine's keyboard or mouse ends the remote session: the
  monitors come back and the desktop locks (Omarchy's lock screen).
- Ending or disconnecting the RDP session (closing the window) also locks
  the desktop. Closing the window keeps it attached until the next login or
  local input.
- If the desktop was locked, the RDP window shows the lock screen; unlock it
  with your password.

The user needs to be in `tsusers` like any RDP user. Set `CONSOLE=no` in
`~/.config/omarchy/xrdp.conf` to get a separate session instead (only
sensible while not logged in at the machine).

Per-user settings go in `~/.config/omarchy/xrdp.conf`:

```bash
DESKTOP=omarchy     # xorg: use stock Xorg/xorgxrdp (e.g. XFCE via ~/.xinitrc) instead
SCALE=auto          # auto: 1 below 2560 px wide, 1.5 below 3200, else 2
KEYBOARD=           # xkb layout overriding the client's (e.g. "de")
RENDER_NODE=/dev/dri/renderD128
BACKEND_DEBUG=      # 1: log frame rate, timings, clipboard and key events
CONSOLE=yes         # no: never attach to a desktop already running locally
MAC_SHORTCUTS=      # yes: Windows App on macOS sends Command+C/X/V as Ctrl+C/X/V;
                    # turn those back into Super+C/X/V (Omarchy copy/cut/paste,
                    # which also works in terminals). A real Control+C stays Ctrl.
```

Logs: `~/.local/state/omarchy-xrdp/session-<display>.log` for each session,
plus the usual `/var/log/xrdp*.log`. `omarchy-xrdp status` shows the setup
and running sessions.

## Performance

Measured on this setup (Core Ultra 9 285, Intel graphics) with a full-screen
terminal scrolling random text at 1920×1080, a near worst case for video
encoding:

| | Earlier VNC-based version | Native backend |
|---|---|---|
| Bandwidth to the client | 96.5 Mbit/s | 13–39 Mbit/s (H.264) |
| Frames delivered | ~4/s | ~30/s (bounded by the test content) |
| Backend time per frame | — | ~3 ms convert + send, ~4 ms until xrdp has encoded it |

Normal desktop use needs far less bandwidth. xrdp's encoder settings live in
`/etc/xrdp/gfx.toml` (x264 `ultrafast`/`zerolatency` by default).

## Limitations

- **One RDP session per user** is the sensible setup; Omarchy's systemd
  `--user` environment is shared between a user's sessions, and so is the
  xrdp sound sink (pipewire-module-xrdp).
- **Single monitor.** Multi-monitor clients get one screen.
- **Clipboard** is text only.
- XDG autostart entries (`~/.config/autostart`, `/etc/xdg/autostart`) don't
  run; Omarchy's own autostart (in the Hyprland config) does.

## Uninstall

```
sudo omarchy-xrdp remove
sudo pacman -R omarchy-xrdp
```

## Development

Build the backend with `meson setup backend/build backend && ninja -C
backend/build`. `tests/` runs everything without root:

- `tests/stage.sh` copies the scripts and the built backend to
  `/tmp/omxrdp-test`; `start-test-session.sh` launches a session the way
  xrdp-sesman does (`sesman-emulate.sh`) as a transient `systemd --user`
  unit of the test user (who needs lingering, `loginctl enable-linger`).
- An unprivileged xrdp with `lib=libxup.so` and `port=` set to the session's
  socket serves it on another port; `tests/rdp-client.sh` runs FreeRDP
  (`RDP_CLIENT=xfreerdp3`) inside a private headless cage, driven with
  xdotool and captured with grim (`tests/fetch-tools.sh` fetches the tools).
  The first input event after connecting only gives that client focus.
- `tests/e2e.sh` checks session start, resize, typing, Super+drag, clipboard,
  reload and logout; `tests/perf.sh` measures bandwidth and CPU under load;
  `tests/ini-roundtrip.sh` checks `setup`/`remove` against copies of the
  installed ini files.
- `omarchy-xrdp-backend --bench SECONDS` measures capture alone.
