#!/bin/bash
# desktop.sh — desktop.sh dock-icon
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=lib/blocks-common.sh
. "$HERE/lib/blocks-common.sh"
ROOT="$(blocks_resolve_root "$0")"
LIB="$(blocks_scripts_lib "$0")"
SUB="${1:-}"
shift || true
case "$SUB" in
    dock-icon)
        # shellcheck source=/dev/null
        . "$LIB/dock-icon.sh"
        install_blocks_dock_icon "$@"
        ;;
    *) echo "usage: desktop.sh dock-icon" >&2; exit 1 ;;
esac
