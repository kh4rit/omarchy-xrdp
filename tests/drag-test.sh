#!/bin/bash
# Super+left-drag (Command-drag on a Mac client) through the RDP client must
# move a window. Expects a running test session on display 20 and the test
# client connected to it.
# Usage: drag-test.sh TEST_USER
set -u
user=$1
stage=${STAGE:-/tmp/omxrdp-test}
client=$stage/tests/rdp-client.sh
as_user() { sudo -n -u "$user" "$@"; }
windows() { as_user "$stage/tests/hypr.sh" 20 -j clients | jq -r 'sort_by(.at[0]) | .[] | "\(.address) \(.at[0])"'; }

# Two tiled terminals side by side
as_user "$stage/tests/launch.sh" 20 "omarchy-launch-terminal" >/dev/null
sleep 1.5
as_user "$stage/tests/launch.sh" 20 "omarchy-launch-terminal" >/dev/null
sleep 2
before=$(windows)
left=$(head -1 <<<"$before" | cut -d' ' -f1)
echo "before:"; echo "$before"

# The first input after connecting only focuses the X11 test client
"$client" xtype mousemove 400 450 click 1
sleep 0.5

"$client" xtype keydown super
sleep 0.2
"$client" xtype mousemove 400 450 mousedown 1
for x in 500 650 800 950 1100 1250; do
  "$client" xtype mousemove "$x" 450
  sleep 0.05
done
"$client" xtype mouseup 1
sleep 0.1
"$client" xtype keyup super
sleep 1

after=$(windows)
echo "after:"; echo "$after"
x_after=$(grep "^$left " <<<"$after" | cut -d' ' -f2)
if [[ -n $x_after ]] && ((x_after > 600)); then
  echo "PASS Super+drag moved the left window to the right"
else
  echo "FAIL Super+drag did not move the window"
  exit 1
fi
