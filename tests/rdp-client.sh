#!/bin/bash
# Test RDP client: runs sdl-freerdp3 inside a private headless cage, so a
# test can drive it (wtype) and screenshot it (grim) without any real screen.
#
# Usage: rdp-client.sh start HOST:PORT WxH [extra freerdp args...]
#        rdp-client.sh shot OUT.png
#        rdp-client.sh type ARGS...    (passed to wtype)
#        rdp-client.sh xtype ARGS...   (passed to xdotool; for RDP_CLIENT=xfreerdp3)
#        rdp-client.sh stop
set -u

stage=${STAGE:-/tmp/omxrdp-test}
dir=${CLIENT_DIR:-/tmp/omxrdp-client-$(id -u)}
export XDG_RUNTIME_DIR=/run/user/$(id -u)

case ${1:-} in
  start)
    target=$2 size=$3
    shift 3
    mkdir -p "$dir"
    rm -f "$dir/wayland"
    WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_LIBINPUT_NO_DEVICES=1 \
      WLR_RENDER_DRM_DEVICE=/dev/dri/renderD128 \
      LD_LIBRARY_PATH=$stage/prefix/usr/lib \
      setsid "$stage/lib/cage" -- sh -c '
        echo "${DISPLAY:-}" >"$0/x11"
        echo "$WAYLAND_DISPLAY" >"$0/wayland"
        LD_LIBRARY_PATH=$1/prefix/usr/lib $1/prefix/usr/bin/wlr-randr --output HEADLESS-1 --custom-mode "$2"
        shift 2
        unset LD_LIBRARY_PATH
        [ -n "${CLIENT_APP:-}" ] && exec $CLIENT_APP
        exec env SDL_VIDEODRIVER=wayland ${RDP_CLIENT:-sdl-freerdp3} "$@"
      ' "$dir" "$stage" "$size" "/v:$target" /u:test /p:test /cert:ignore "/size:$size" "$@" \
      >"$dir/client.log" 2>&1 &
    echo $! >"$dir/pid"
    for _ in $(seq 50); do [[ -s $dir/wayland ]] && break; sleep 0.1; done
    echo "client started on $(cat "$dir/wayland" 2>/dev/null)"
    ;;
  shot)
    WAYLAND_DISPLAY=$(<"$dir/wayland") grim "$2"
    ;;
  type)
    shift
    WAYLAND_DISPLAY=$(<"$dir/wayland") wtype "$@"
    ;;
  resize)
    # Resize the client's screen; with /dynamic-resolution FreeRDP follows
    WAYLAND_DISPLAY=$(<"$dir/wayland") LD_LIBRARY_PATH=$stage/prefix/usr/lib \
      "$stage/prefix/usr/bin/wlr-randr" --output HEADLESS-1 --custom-mode "$2"
    ;;
  xtype)
    shift
    DISPLAY=$(<"$dir/x11") LD_LIBRARY_PATH=$stage/prefix/usr/lib "$stage/prefix/usr/bin/xdotool" "$@"
    ;;
  stop)
    [[ -f $dir/pid ]] && kill -- -"$(<"$dir/pid")" 2>/dev/null
    rm -f "$dir/pid" "$dir/wayland"
    ;;
  *)
    echo "Usage: rdp-client.sh start HOST:PORT WxH | shot OUT.png | type ARGS... | stop" >&2
    exit 1
    ;;
esac
