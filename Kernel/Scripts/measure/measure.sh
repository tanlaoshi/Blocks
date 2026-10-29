#!/bin/bash
# measure.sh — measure.sh boot [opts]
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=../lib/blocks-common.sh
. "$HERE/../lib/blocks-common.sh"
ROOT="$(blocks_resolve_root "$0")"
LIB="$(blocks_scripts_lib "$0")"
SUB="${1:-boot}"
shift || true
case "$SUB" in
    boot) "$LIB/measure-boot.sh" "$@" ;;
    *) echo "usage: measure.sh boot [opts]" >&2; exit 1 ;;
esac
