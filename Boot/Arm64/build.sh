#!/bin/bash
#
# build.sh — 单独编译 Boot/Arm64（不链完整 Kernel）
#
# 产出：Build/Boot_asm.o、Build/Boot.o
# 头文件：仅本目录（-I. → BootInfo.h / BootTypes.h），不依赖 Kernel/Include。
#
# 交叉工具链（与 Kernel 共用）：OpenBox/Tools/Extract/xpack-aarch64-*
#   可用 TOOLS_ROOT 覆盖 Extract 根；找不到再试 PATH。
#
# 环境变量：
#   BRINGUP=1       → -DTOY_BRINGUP=1，BootMain 空转
#   TOOLS_ROOT=…    → 含 xpack-aarch64-none-elf-gcc-* 的目录
#
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
OPENBOX_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
OUT_DIR="$SCRIPT_DIR/Build"
INC_DIR="$SCRIPT_DIR"
TOOLS_ROOT="${TOOLS_ROOT:-$OPENBOX_ROOT/Tools/Extract}"
BRINGUP="${BRINGUP:-0}"
mkdir -p "$OUT_DIR"

pick_cc() {
    local latest c
    latest="$(ls -d "$TOOLS_ROOT"/xpack-aarch64-none-elf-gcc-*/bin/aarch64-none-elf-gcc 2>/dev/null | sort | tail -1 || true)"
    if [ -n "$latest" ] && [ -x "$latest" ]; then
        echo "$latest"
        return
    fi
    for c in aarch64-none-elf-gcc aarch64-linux-gnu-gcc; do
        if command -v "$c" >/dev/null 2>&1; then
            command -v "$c"
            return
        fi
    done
    echo "error: 找不到 aarch64 交叉编译器" >&2
    echo "  请按 $OPENBOX_ROOT/Tools/README.md 放入 Tools/Extract，或设置 TOOLS_ROOT" >&2
    exit 1
}
CC="$(pick_cc)"

CFLAGS=(
    -ffreestanding -nostdlib -O2 -Wall -Wextra
    -fno-stack-protector -fno-builtin -fno-pie -fno-pic
    -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0
    -DTOY_BRINGUP="$BRINGUP"
    -mgeneral-regs-only
    -I"$INC_DIR"
)

cd "$SCRIPT_DIR"
"$CC" -DTOY_BRINGUP="$BRINGUP" -mgeneral-regs-only -c Boot.S -o "$OUT_DIR/Boot_asm.o"
"$CC" "${CFLAGS[@]}" -c Boot.c -o "$OUT_DIR/Boot.o"

echo "=========================================="
echo "Boot/Arm64 OK  BRINGUP=$BRINGUP  (→ KernelMain(BOOT_INFO*))"
echo "CC=$CC  INC=$INC_DIR"
ls -lh "$OUT_DIR/Boot_asm.o" "$OUT_DIR/Boot.o"
echo "=========================================="
