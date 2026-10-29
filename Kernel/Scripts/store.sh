#!/bin/bash
# store.sh — store.sh <export|serve> [out-dir]
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=lib/blocks-common.sh
. "$HERE/lib/blocks-common.sh"
ROOT="$(blocks_resolve_root "$0")"
SUB="${1:-}"
shift || true
T="$ROOT/ToyKernel/Tools/Scripts"
case "$SUB" in
    export) bash "$T/export-store-lan.sh" "$@" ;;
    serve) bash "$T/serve-store-lan.sh" "$@" ;;
    *) echo "usage: store.sh <export|serve> [args]" >&2; exit 1 ;;
esac
