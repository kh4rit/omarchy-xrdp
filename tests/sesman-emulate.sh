#!/bin/bash
# Launch omarchy-xrdp-session the way xrdp-sesman does (bare env, Xvnc-style
# args), as the current user, then run xrdp's waitforx against it.
# Usage: sesman-emulate.sh DISPLAY SOCKET [WxH]
set -u
display=$1 socket=$2 geometry=${3:-1920x1080}
uid=$(id -u)
cookie=$(od -An -N16 -tx1 /dev/urandom | tr -d ' \n')
xauth -f "$HOME/.Xauthority" add ":$display" MIT-MAGIC-COOKIE-1 "$cookie"
env -i HOME="$HOME" USER="$USER" LOGNAME="$USER" SHELL=/bin/bash UID="$uid" \
  PATH=/sbin:/bin:/usr/bin:/usr/local/bin DISPLAY=":$display.0" \
  XRDP_SESSION=$$ XRDP_SOCKET_PATH="/run/xrdp/$uid" \
  XRDP_PULSE_SINK_SOCKET="xrdp_chansrv_audio_out_socket_$display" \
  XRDP_PULSE_SOURCE_SOCKET="xrdp_chansrv_audio_in_socket_$display" \
  ${OMARCHY_XRDP_LIBDIR:+OMARCHY_XRDP_LIBDIR=$OMARCHY_XRDP_LIBDIR} \
  ${LD_LIBRARY_PATH:+LD_LIBRARY_PATH=$LD_LIBRARY_PATH} \
  "${OMARCHY_XRDP_LIBDIR:-/usr/lib/omarchy-xrdp}/omarchy-xrdp-session" ":$display" \
  -auth .Xauthority -geometry "$geometry" -depth 24 \
  -rfbport -1 -rfbunixpath "$socket" -rfbunixmode 432 -SecurityTypes None \
  -bs -nolisten tcp -localhost -dpi 96 &
echo "session pid $!"
env -i HOME="$HOME" DISPLAY=":$display" /usr/lib/xrdp/waitforx -d ":$display"
echo "waitforx exit $?"
# Keep running for as long as the session does (so a systemd unit wrapping
# this script lives exactly as long as the session)
wait
