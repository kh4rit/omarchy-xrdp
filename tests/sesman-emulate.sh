#!/bin/bash
# Emulate xrdp-sesman's session flow for an Xorg-type (code=20) session, as
# the current user:
#   1. start the "X server" (omarchy-xrdp-xserver, sesman.ini [Xorg] param)
#      with a bare environment and Xorg's arguments; -socket (test only)
#      puts the backend socket where a private xrdp can reach it, and
#      -geometry stands in for the size xrdp sends when it connects
#   2. wait for its X display with xrdp's own waitforx
#   3. run the window manager step (omarchy-xrdp-startwm) with the login
#      environment and XRDP_* variables
#   4. when the window manager exits, SIGTERM the X server
# Usage: sesman-emulate.sh DISPLAY SOCKET [WxH]
set -u
display=$1 socket=$2 geometry=${3:-1920x1080}
uid=$(id -u)
libdir=${OMARCHY_XRDP_LIBDIR:-/usr/lib/omarchy-xrdp}

cookie=$(od -An -N16 -tx1 /dev/urandom | tr -d ' \n')
xauth -f "$HOME/.Xauthority" add ":$display" MIT-MAGIC-COOKIE-1 "$cookie"

xrdp_env=(
  HOME="$HOME" USER="$USER" LOGNAME="$USER" SHELL=/bin/bash UID="$uid"
  PATH=/sbin:/bin:/usr/bin:/usr/local/bin DISPLAY=":$display.0"
  XRDP_SESSION=$$ XRDP_SOCKET_PATH="/run/xrdp/$uid"
  XRDP_PULSE_SINK_SOCKET="xrdp_chansrv_audio_out_socket_$display"
  XRDP_PULSE_SOURCE_SOCKET="xrdp_chansrv_audio_in_socket_$display"
)
[[ -n ${LD_LIBRARY_PATH:-} ]] && xrdp_env+=(LD_LIBRARY_PATH="$LD_LIBRARY_PATH")
# Test overrides (OMARCHY_XRDP_LIBDIR, _ATTACH, _OUTPUT, _LOCK, ...)
while IFS= read -r var; do
  xrdp_env+=("$var=${!var}")
done < <(compgen -e | grep '^OMARCHY_XRDP_')

env -i "${xrdp_env[@]}" "$libdir/omarchy-xrdp-xserver" ":$display" \
  -auth .Xauthority -config xrdp/xorg.conf -noreset -nolisten tcp \
  -logfile .xorgxrdp.%s.log -geometry "$geometry" -socket "$socket" &
xserver=$!
echo "X server pid $xserver"

if ! env -i HOME="$HOME" DISPLAY=":$display" /usr/lib/xrdp/waitforx -d ":$display"; then
  echo "waitforx failed"
  kill "$xserver"
  wait
  exit 1
fi

# pam_systemd's variables are part of the window manager's environment
env -i "${xrdp_env[@]}" XDG_RUNTIME_DIR="/run/user/$uid" \
  DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$uid/bus" \
  "$libdir/omarchy-xrdp-startwm" &
wm=$!
echo "window manager pid $wm"

trap 'kill "$wm" 2>/dev/null' TERM INT
wait "$wm"
echo "window manager exited, stopping X server"
kill -TERM "$xserver" 2>/dev/null
wait "$xserver"
