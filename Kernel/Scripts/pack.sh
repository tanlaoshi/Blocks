#!/bin/bash
# pack.sh — pack.sh <id> <elf-path> [file-name]
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=lib/blocks-common.sh
. "$HERE/lib/blocks-common.sh"
ROOT="$(blocks_resolve_root "$0")"
bash "$ROOT/ToyKernel/Tools/Scripts/pack-app.sh" "$@"
