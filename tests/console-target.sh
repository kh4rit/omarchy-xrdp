#!/bin/bash
# Run as the test user: a throwaway Hyprland (minimal config, nested in a
# headless cage) standing in for a live local desktop in console-mode tests.
# It never touches the user's systemd --user session.
# Usage: console-target.sh start|stop|hyprctl ARGS...|local-input
stage=${STAGE:-/tmp/omxrdp-test}
dir=$stage/console-target-$(id -u)
export XDG_RUNTIME_DIR=/run/user/$(id -u)
case $1 in
  start)
    rm -rf "$dir"
    mkdir -p "$dir"
    cat >"$dir/hyprland.lua" <<EOC
hl.config({
  misc = { key_press_enables_dpms = true, mouse_move_enables_dpms = true,
           disable_hyprland_logo = true },
  ecosystem = { no_update_news = true, no_donation_nag = true },
})
hl.bind("SUPER + Return", hl.dsp.exec_cmd("touch $dir/remote-key"))
-- Stands in for Omarchy's universal paste
hl.bind("SUPER + V", hl.dsp.exec_cmd("touch $dir/super-v"))
EOC
    systemctl --user stop omxrdp-console-target 2>/dev/null
    systemd-run --user --quiet --collect --unit=omxrdp-console-target \
      --setenv=WLR_BACKENDS=headless --setenv=WLR_HEADLESS_OUTPUTS=1 \
      --setenv=WLR_LIBINPUT_NO_DEVICES=1 --setenv=WLR_RENDER_DRM_DEVICE=/dev/dri/renderD128 \
      --setenv=XCURSOR_PATH="$stage/icons:/usr/share/icons" \
      "$stage/lib/cage" -- sh -c 'exec Hyprland --config "$1/hyprland.lua"' sh "$dir"
    for _ in $(seq 100); do
      for lock in "$XDG_RUNTIME_DIR"/hypr/*/hyprland.lock; do
        pid=$(sed -n 1p "$lock" 2>/dev/null)
        [[ -n $pid && $(tr '\0' ' ' <"/proc/$pid/cmdline" 2>/dev/null) == *"$dir/hyprland.lua"* ]] || continue
        basename "$(dirname "$lock")" >"$dir/sig"
        sed -n 2p "$lock" >"$dir/wayland"
        sleep 1
        # The "machine's own" monitor
        HYPRLAND_INSTANCE_SIGNATURE=$(<"$dir/sig") hyprctl output create headless LOCAL-1 >/dev/null
        sleep 1
        echo "target $(<"$dir/sig")"
        exit 0
      done
      sleep 0.1
    done
    echo "target Hyprland did not start" >&2
    exit 1
    ;;
  stop)
    systemctl --user stop omxrdp-console-target 2>/dev/null
    pkill -u "$(id -u)" -f "Hyprland --config $dir/hyprland.lua"
    ;;
  hyprctl)
    shift
    HYPRLAND_INSTANCE_SIGNATURE=$(<"$dir/sig") hyprctl "$@"
    ;;
  local-input)
    # Input from a device that isn't the backend's: the "machine's own" keyboard
    WAYLAND_DISPLAY=$(<"$dir/wayland") wtype -k Shift_L
    ;;
esac
