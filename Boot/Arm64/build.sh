#!/bin/bash
# Boot/Arm64：Boot.S + Boot.c → .o；头文件仅本目录 BootInfo.h
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
OPENBOX_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
OUT_DIR="$SCRIPT_DIR/Build"
INC_DIR="$SCRIPT_DIR"
TOOLS_ROOT="${TOOLS_ROOT:-$OPENBOX_ROOT/Tools/Extract}"
BRINGUP="${BRINGUP:-0}"
mkdir -p "$OUT_DIR"

pick_cc() {
    local latest
    latest="$(ls -d "$TOOLS_ROOT"/xpack-aarch64-none-elf-gcc-*/bin/aarch64-none-elf-gcc 2>/dev/null | sort | tail -1 || true)"
    if [ -n "$latest" ] && [ -x "$latest" ]; then
        echo "$latest"
        return
    fi
    for c in aarch64-linux-gnu-gcc aarch64-none-elf-gcc; do
        if command -v "$c" >/dev/null 2>&1; then
            command -v "$c"
            return
        fi
    done
    echo "error: 找不到 aarch64 交叉编译器（见 $OPENBOX_ROOT/Tools/README.md）" >&2
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
