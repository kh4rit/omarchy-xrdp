#!/bin/bash
# Run as the test user: check the clipboard bridge between session DISPLAY's
# Hyprland and its X display (where xrdp's chansrv serves the RDP
# clipboard), using xclip in chansrv's place.
# Usage: clip-test.sh DISPLAY
stage=${STAGE:-/tmp/omxrdp-test}
export XDG_RUNTIME_DIR=/run/user/$(id -u)
export WAYLAND_DISPLAY=$(<"$XDG_RUNTIME_DIR/omarchy-xrdp/$1/wayland")
export DISPLAY=:$1
xclip() { LD_LIBRARY_PATH=$stage/tools/usr/lib "$stage/tools/usr/bin/xclip" "$@"; }
fail=0

# RDP client copies: chansrv owns the X CLIPBOARD
from_x="copied on the client $RANDOM ✓"
printf '%s' "$from_x" | xclip -selection clipboard -i
sleep 1
got=$(wl-paste --no-newline 2>/dev/null)
if [[ $got == "$from_x" ]]; then echo "PASS client → Omarchy"; else echo "FAIL client → Omarchy: got '$got'"; fail=1; fi

# An Omarchy app copies: the bridge must take the X CLIPBOARD for chansrv
from_wl="copied in Omarchy $RANDOM ✓"
printf '%s' "$from_wl" | wl-copy
sleep 1
got=$(xclip -selection clipboard -o 2>/dev/null)
if [[ $got == "$from_wl" ]]; then echo "PASS Omarchy → client"; else echo "FAIL Omarchy → client: got '$got'"; fail=1; fi

# And back the other way once more (ownership changes hands again)
again="second client copy $RANDOM"
printf '%s' "$again" | xclip -selection clipboard -i
sleep 1
got=$(wl-paste --no-newline 2>/dev/null)
if [[ $got == "$again" ]]; then echo "PASS client → Omarchy again"; else echo "FAIL client → Omarchy again: got '$got'"; fail=1; fi
exit $fail
