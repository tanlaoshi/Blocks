#!/bin/bash
# sync.sh — sync.sh <usb|nuc|kernel> …
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=../lib/blocks-common.sh
. "$HERE/../lib/blocks-common.sh"
ROOT="$(blocks_resolve_root "$0")"
LIB="$(blocks_scripts_lib "$0")"
SUB="${1:-}"
shift || true
case "$SUB" in
    usb) "$LIB/sync-usb.sh" "$@" ;;
    nuc) "$LIB/sync-nuc.sh" "$@" ;;
    kernel) "$LIB/sync-kernel-usb.sh" "$@" ;;
    *) echo "usage: sync.sh <usb|nuc|kernel> [opts]" >&2; exit 1 ;;
esac
