#!/bin/bash
# 已废弃单盘启动：统一走双盘 run-split.sh（Kernel/THEME 只从第二盘 RootFs/X64 读）
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=blocks-common.sh
. "$SCRIPT_DIR/blocks-common.sh"
IMAGE_ROOT="$(blocks_image_root "$0")"
cd "$IMAGE_ROOT"
echo "note: run.sh -> run-split.sh (ESP + RootFs/X64; payloads on disk1 only)" >&2
exec "$SCRIPT_DIR/run-split.sh" "$@"
