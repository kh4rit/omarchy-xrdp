#!/bin/bash
# Run an unprivileged xrdp for tests with its own gfx.toml: xrdp always
# reads /etc/xrdp/gfx.toml, so a user + mount namespace shows it GFX_TOML
# there instead (no root needed; the system's file is untouched).
# Usage: private-xrdp.sh XRDP_INI [GFX_TOML]
ini=$1 gfx=${2:-}
if [[ -n $gfx && -z ${_PRIVATE_XRDP_NS:-} ]]; then
  exec env _PRIVATE_XRDP_NS=1 unshare -Urm "$0" "$@"
fi
if [[ -n $gfx ]]; then
  mount --bind "$gfx" /etc/xrdp/gfx.toml || exit 1
fi
exec xrdp --nodaemon --config "$ini"
