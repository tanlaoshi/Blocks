#!/bin/bash
#
# build.sh — 链到 KernelMain 门口（三架构）
#
# 用法：./build.sh [x64|arm64|riscv]
# 产出：Build/<Arch>/Kernel.elf
#
# 原则：凡进 Kernel.elf 的源码都在 Kernel/（含各 Arch 的 KernelEntry/KernelHandoff）。
#       顶层 Boot/ 只放独立 EFI（X64）。
#
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
OPENBOX_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
ARCH="${1:-x64}"
TOOLS_ROOT="${TOOLS_ROOT:-$OPENBOX_ROOT/Tools/Extract}"
OUT_ROOT="$SCRIPT_DIR/Build"
INC_ABI="$SCRIPT_DIR/Include/Abi"
INC_CORE="$SCRIPT_DIR/Include/Core"
INC_HAL="$SCRIPT_DIR/Include/Hal"

COMMON_CFLAGS=(
    -ffreestanding -nostdlib -O2 -Wall -Wextra
    -fno-stack-protector -fno-builtin -fno-pie -fno-pic
    -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0
    -DTOY_BRINGUP=0
    -I"$INC_ABI" -I"$INC_CORE" -I"$INC_HAL"
)

pick_x64_cc() {
    command -v gcc >/dev/null 2>&1 && { command -v gcc; return; }
    echo "error: 需要本机 gcc（X64）" >&2
    exit 1
}

pick_arm_cc() {
    local latest
    latest="$(ls -d "$TOOLS_ROOT"/xpack-aarch64-none-elf-gcc-*/bin/aarch64-none-elf-gcc 2>/dev/null | sort | tail -1 || true)"
    if [ -n "$latest" ] && [ -x "$latest" ]; then
        echo "$latest"
        return
    fi
    for c in aarch64-none-elf-gcc aarch64-linux-gnu-gcc; do
        command -v "$c" >/dev/null 2>&1 && { command -v "$c"; return; }
    done
    echo "error: 找不到 aarch64 交叉编译器（见 Tools/README.md）" >&2
    exit 1
}

pick_riscv_cc() {
    local latest
    latest="$(ls -d "$TOOLS_ROOT"/xpack-riscv-none-elf-gcc-*/bin/riscv-none-elf-gcc 2>/dev/null | sort | tail -1 || true)"
    if [ -n "$latest" ] && [ -x "$latest" ]; then
        echo "$latest"
        return
    fi
    for c in riscv-none-elf-gcc riscv64-unknown-elf-gcc riscv64-linux-gnu-gcc; do
        command -v "$c" >/dev/null 2>&1 && { command -v "$c"; return; }
    done
    echo "error: 找不到 riscv 交叉编译器（见 Tools/README.md）" >&2
    exit 1
}

build_common_objs() {
    local cc="$1"
    shift
    local out="$1"
    shift
    local -a cflags=("$@")
    "$cc" "${cflags[@]}" -c "$SCRIPT_DIR/Core/BootInfo.c" -o "$out/BootInfo.o"
    "$cc" "${cflags[@]}" -c "$SCRIPT_DIR/Core/KernelMain.c" -o "$out/KernelMain.o"
}

case "$ARCH" in
x64|X64)
    ARCH=X64
    OUT="$OUT_ROOT/X64"
    mkdir -p "$OUT"
    CC="$(pick_x64_cc)"
    CFLAGS=("${COMMON_CFLAGS[@]}" -m64 -mno-red-zone -mgeneral-regs-only
            -I"$SCRIPT_DIR/Hal/X64" -I"$OPENBOX_ROOT/Boot/BootPkg")
    build_common_objs "$CC" "$OUT" "${CFLAGS[@]}"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/X64/KernelEntry.S" -o "$OUT/KernelEntry.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/X64/KernelHandoff.c" -o "$OUT/KernelHandoff.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/X64/EarlyIdentity.c" -o "$OUT/EarlyIdentity.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/X64/PlatformStub.c" -o "$OUT/PlatformStub.o"
    "$CC" -nostdlib -ffreestanding -no-pie \
        -Wl,-T,"$SCRIPT_DIR/Hal/X64/link.ld" \
        -o "$OUT/Kernel.elf" \
        "$OUT/KernelEntry.o" "$OUT/KernelHandoff.o" "$OUT/EarlyIdentity.o" \
        "$OUT/PlatformStub.o" "$OUT/BootInfo.o" "$OUT/KernelMain.o"
    ;;
arm64|Arm64|ARM64)
    ARCH=Arm64
    OUT="$OUT_ROOT/Arm64"
    mkdir -p "$OUT"
    CC="$(pick_arm_cc)"
    CFLAGS=("${COMMON_CFLAGS[@]}" -mgeneral-regs-only)
    build_common_objs "$CC" "$OUT" "${CFLAGS[@]}"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/Arm64/KernelEntry.S" -o "$OUT/KernelEntry.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/Arm64/KernelHandoff.c" -o "$OUT/KernelHandoff.o"
    "$CC" -nostdlib -ffreestanding -no-pie \
        -Wl,-T,"$SCRIPT_DIR/Hal/Arm64/link.ld" \
        -o "$OUT/Kernel.elf" \
        "$OUT/KernelEntry.o" "$OUT/KernelHandoff.o" \
        "$OUT/BootInfo.o" "$OUT/KernelMain.o"
    ;;
riscv|RiscV|RISCV)
    ARCH=RiscV
    OUT="$OUT_ROOT/RiscV"
    mkdir -p "$OUT"
    CC="$(pick_riscv_cc)"
    ARCH_CFLAGS=(-march=rv64imac_zicsr_zifencei -mabi=lp64 -mcmodel=medany)
    CFLAGS=("${COMMON_CFLAGS[@]}" "${ARCH_CFLAGS[@]}")
    build_common_objs "$CC" "$OUT" "${CFLAGS[@]}"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/RiscV/KernelEntry.S" -o "$OUT/KernelEntry.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/RiscV/KernelHandoff.c" -o "$OUT/KernelHandoff.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/RiscV/SmpStub.c" -o "$OUT/SmpStub.o"
    "$CC" -nostdlib -ffreestanding -no-pie "${ARCH_CFLAGS[@]}" \
        -Wl,-T,"$SCRIPT_DIR/Hal/RiscV/link.ld" \
        -o "$OUT/Kernel.elf" \
        "$OUT/KernelEntry.o" "$OUT/KernelHandoff.o" "$OUT/SmpStub.o" \
        "$OUT/BootInfo.o" "$OUT/KernelMain.o"
    ;;
*)
    echo "usage: $0 [x64|arm64|riscv]" >&2
    exit 1
    ;;
esac

echo "=========================================="
echo "Kernel/$ARCH OK  → $OUT/Kernel.elf  (停在 KernelMain 桩)"
echo "CC=$CC"
ls -lh "$OUT/Kernel.elf"
echo "=========================================="
