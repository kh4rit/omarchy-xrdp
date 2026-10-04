#!/bin/bash
# Dev helper: copy the working tree's scripts, the built backend and a cage
# into a world-readable staging dir so they can be run as the test user.
# Usage: tests/stage.sh [CAGE_BINARY]   (default: the installed package's cage)
set -eu
root=$(dirname "$(readlink -f "$0")")/..
stage=${STAGE:-/tmp/omxrdp-test}
mkdir -p "$stage/lib/shims" "$stage/tests" "$stage/systemd"
cp --preserve=mode "$root"/bin/* "$stage/lib/"
cp --preserve=mode "$root"/shims/* "$stage/lib/shims/"
cp --preserve=mode "$root"/tests/*.sh "$root"/tests/*.py "$stage/tests/"
cp "$root"/systemd/* "$stage/systemd/"
if [[ -x $root/backend/build/omarchy-xrdp-backend ]]; then
  cp "$root/backend/build/omarchy-xrdp-backend" "$stage/lib/.backend.new"
  mv -f "$stage/lib/.backend.new" "$stage/lib/omarchy-xrdp-backend"
fi
cage=${1:-/usr/lib/omarchy-xrdp/cage}
# copy then rename: the running binary may be busy
if [[ -x $cage ]]; then
  cp "$cage" "$stage/lib/.cage.new"
  mv -f "$stage/lib/.cage.new" "$stage/lib/cage"
fi
# A private wlroots build, if the system doesn't have wlroots0.20
ld_path=""
[[ -d $stage/prefix/usr/lib ]] && ld_path=$stage/prefix/usr/lib
cat >"$stage/start" <<EOS
#!/bin/bash
export OMARCHY_XRDP_LIBDIR=$stage/lib
${ld_path:+export LD_LIBRARY_PATH=$ld_path}
exec $stage/tests/start-test-session.sh "\$@"
EOS
cat >"$stage/stop" <<EOS
#!/bin/bash
exec $stage/tests/stop-test-session.sh "\$@"
EOS
chmod 755 "$stage/start" "$stage/stop"
find "$stage" -user "$(id -u)" -exec chmod a+rX {} +
chmod 1777 "$stage"  # the test user creates its session sockets here
echo "staged into $stage"
