#!/bin/bash
# Run as the test user: print HEADLESS-RDP's size every 0.1s whenever it
# changes, for SECONDS seconds. Usage: watch-size.sh DISPLAY SECONDS
export XDG_RUNTIME_DIR=/run/user/$(id -u)
export HYPRLAND_INSTANCE_SIGNATURE=$(<"$XDG_RUNTIME_DIR/omarchy-xrdp/$1/hyprland-instance")
end=$((SECONDS + $2))
last=""
while ((SECONDS < end)); do
  size=$(hyprctl -j monitors 2>/dev/null | jq -r '.[] | select(.name == "HEADLESS-RDP") | "\(.width)x\(.height)"')
  if [[ $size != "$last" ]]; then
    echo "$(date +%T.%N | cut -c1-12) $size"
    last=$size
  fi
  sleep 0.1
done
