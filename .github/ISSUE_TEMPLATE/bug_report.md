---
name: Bug report
about: Something doesn't work in an Omarchy RDP session
---

**What happened, and what you expected**


**Client**: (e.g. Windows App 11 on macOS 15, Windows Remote Desktop, Remmina, FreeRDP)
**Resolution / scaling**: (e.g. 3840x2160 "high definition")
**omarchy-xrdp version**: (`pacman -Q omarchy-xrdp`)
**xrdp / Hyprland versions**: (`pacman -Q xrdp hyprland`)
**Graphics**: (e.g. Intel Arc, AMD, NVIDIA)

**Session log**: attach `~/.local/state/omarchy-xrdp/session-<display>.log`
from the affected session (it contains no keystrokes unless `BACKEND_DEBUG`
is set in `~/.config/omarchy/xrdp.conf`), and if relevant the matching part
of `/var/log/xrdp.log` and `/var/log/xrdp-sesman.log`.
