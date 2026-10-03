#!/bin/bash
# Dev helper: copy the working tree's scripts (and a built cage) into a
# world-readable staging dir so they can be run as the test user.
# Usage: tests/stage.sh [CAGE_BINARY]
set -eu
root=$(dirname "$(readlink -f "$0")")/..
stage=${STAGE:-/tmp/omxrdp-test}
mkdir -p "$stage/lib" "$stage/tests"
cp "$root"/bin/* "$stage/lib/"
mkdir -p "$stage/lib/shims"
cp "$root"/shims/* "$stage/lib/shims/"
cp "$root"/tests/*.sh "$stage/tests/"
mkdir -p "$stage/systemd"
cp "$root"/systemd/* "$stage/systemd/"
[[ -n ${1:-} ]] && cp "$1" "$stage/lib/cage"
cat >"$stage/start" <<EOS
#!/bin/bash
export OMARCHY_XRDP_LIBDIR=$stage/lib
export LD_LIBRARY_PATH=$stage/prefix/usr/lib
exec $stage/tests/start-test-session.sh "\$@"
EOS
cat >"$stage/stop" <<EOS
#!/bin/bash
exec $stage/tests/stop-test-session.sh "\$@"
EOS
chmod 755 "$stage/start" "$stage/stop"
chmod -R a+rX "$stage/lib" "$stage/tests" "$stage/systemd" "$stage/start" "$stage/stop"
echo "staged into $stage"
