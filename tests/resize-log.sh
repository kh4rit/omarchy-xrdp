#!/bin/bash
# Run as the test user: resize-related lines (wayvnc debug + proxy) from
# session DISPLAY's log. Usage: resize-log.sh DISPLAY [LINES]
log=${XDG_STATE_HOME:-$HOME/.local/state}/omarchy-xrdp/session-$1.log
grep -aiE 'resiz|layout|master|vnc-proxy|disconnect|client connected|resolution|output-management|configur' "$log" |
  grep -v 'render/egl' | tail -"${2:-30}" | cut -c1-200
