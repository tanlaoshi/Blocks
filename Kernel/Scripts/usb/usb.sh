#!/bin/bash
# usb.sh — usb.sh make [opts]
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=../lib/blocks-common.sh
. "$HERE/../lib/blocks-common.sh"
ROOT="$(blocks_resolve_root "$0")"
LIB="$(blocks_scripts_lib "$0")"
SUB="${1:-make}"
shift || true
case "$SUB" in
    make) "$LIB/make-usb-stick.sh" "$@" ;;
    *) echo "usage: usb.sh make [opts]" >&2; exit 1 ;;
esac
