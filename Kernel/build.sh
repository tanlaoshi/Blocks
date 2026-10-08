#!/bin/bash
#
# build.sh — 链出 Kernel.elf（三架构）
#
# 【初学者】
#   cd ~/Blocks/Kernel
#   ./build.sh x64          # 本机 gcc
#   ./build.sh arm64        # Tools/Extract 里的 aarch64 交叉链
#   ./build.sh riscv        # 同上 riscv 交叉链
#   ./build.sh x64 SERIAL=0        # 编译期关掉 UART
#   ./build.sh x64 SCREEN_LOG=1    # boot 日志镜像到帧缓冲
#
# 产出：Build/<Arch>/Kernel.elf
# 原则：凡进 Kernel.elf 的源码都在 Kernel/；顶层 Boot/ 只放独立 EFI（X64）。
#
set -eo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BLOCKS_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
ARCH="${1:-x64}"
TOOLS_ROOT="${TOOLS_ROOT:-$BLOCKS_ROOT/Tools/Extract}"
OUT_ROOT="$SCRIPT_DIR/Build"
INC_ABI="$SCRIPT_DIR/Include/Abi"
INC_CORE="$SCRIPT_DIR/Include/Core"
INC_HAL="$SCRIPT_DIR/Include/Hal"

# Blocks 接棒期默认开串口；SERIAL=0 可关。SCREEN_LOG 默认关（与现网一致）。
TOY_SERIAL=1
TOY_SCREEN_LOG=0
for Arg in "$@"; do
    case "$Arg" in
        SERIAL=0|serial=0) TOY_SERIAL=0 ;;
        SERIAL=1|serial=1) TOY_SERIAL=1 ;;
        SCREEN_LOG=0|screen_log=0) TOY_SCREEN_LOG=0 ;;
        SCREEN_LOG=1|screen_log=1) TOY_SCREEN_LOG=1 ;;
    esac
done

COMMON_CFLAGS=(
    -ffreestanding -nostdlib -O2 -Wall -Wextra
    -fno-stack-protector -fno-builtin -fno-pie -fno-pic
    -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0
    -DTOY_BRINGUP=0
    -DTOY_SERIAL="$TOY_SERIAL"
    -DTOY_SCREEN_LOG="$TOY_SCREEN_LOG"
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
    local with_video_stub="$1"
    shift
    local -a cflags=("$@")
    local f
    local -a cores=(BootInfo Module KernelModules Font PhysicalMemory Device
                    VirtualMemory KernelMain)
    for f in "${cores[@]}"; do
        "$cc" "${cflags[@]}" -c "$SCRIPT_DIR/Core/${f}.c" -o "$out/${f}.o"
    done
    # Hal 跨 Arch 默认实现放 Hal/Common，不进 Core
    "$cc" "${cflags[@]}" -c "$SCRIPT_DIR/Hal/Common/HalCapability.c" \
        -o "$out/HalCapability.o"
    if [ "$with_video_stub" = 1 ]; then
        "$cc" "${cflags[@]}" -c "$SCRIPT_DIR/Hal/Common/HalVideoStub.c" \
            -o "$out/HalVideoStub.o"
    fi
}

case "$ARCH" in
x64|X64)
    ARCH=X64
    OUT="$OUT_ROOT/X64"
    mkdir -p "$OUT"
    CC="$(pick_x64_cc)"
    CFLAGS=("${COMMON_CFLAGS[@]}" -m64 -mno-red-zone -mgeneral-regs-only
            -I"$SCRIPT_DIR/Hal/X64" -I"$BLOCKS_ROOT/Boot/BootPkg")
    build_common_objs "$CC" "$OUT" 0 "${CFLAGS[@]}"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/X64/KernelEntry.S" -o "$OUT/KernelEntry.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/X64/KernelHandoff.c" -o "$OUT/KernelHandoff.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/X64/EarlyIdentity.c" -o "$OUT/EarlyIdentity.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/X64/PlatformStub.c" -o "$OUT/PlatformStub.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/X64/HalSerial.c" -o "$OUT/HalSerial.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/X64/HalSerialGop.c" -o "$OUT/HalSerialGop.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/X64/HalVideo.c" -o "$OUT/HalVideo.o"
    "$CC" -nostdlib -ffreestanding -no-pie \
        -Wl,-T,"$SCRIPT_DIR/Hal/X64/link.ld" \
        -o "$OUT/Kernel.elf" \
        "$OUT/KernelEntry.o" "$OUT/KernelHandoff.o" "$OUT/EarlyIdentity.o" \
        "$OUT/PlatformStub.o" "$OUT/HalSerial.o" "$OUT/HalSerialGop.o" \
        "$OUT/HalVideo.o" \
        "$OUT/BootInfo.o" "$OUT/Module.o" "$OUT/KernelModules.o" \
        "$OUT/HalCapability.o" "$OUT/Font.o" "$OUT/PhysicalMemory.o" \
        "$OUT/Device.o" "$OUT/VirtualMemory.o" "$OUT/KernelMain.o"
    ;;
arm64|Arm64|ARM64)
    ARCH=Arm64
    OUT="$OUT_ROOT/Arm64"
    mkdir -p "$OUT"
    CC="$(pick_arm_cc)"
    CFLAGS=("${COMMON_CFLAGS[@]}" -mgeneral-regs-only
            -I"$SCRIPT_DIR/Hal/Arm64" -I"$SCRIPT_DIR/Hal/Arm64/Board/virt"
            -I"$SCRIPT_DIR/Hal/Arm64/Hal")
    build_common_objs "$CC" "$OUT" 1 "${CFLAGS[@]}"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/Arm64/KernelEntry.S" -o "$OUT/KernelEntry.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/Arm64/KernelHandoff.c" -o "$OUT/KernelHandoff.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/Arm64/HalSerial.c" -o "$OUT/HalSerial.o"
    "$CC" -nostdlib -ffreestanding -no-pie \
        -Wl,-T,"$SCRIPT_DIR/Hal/Arm64/link.ld" \
        -o "$OUT/Kernel.elf" \
        "$OUT/KernelEntry.o" "$OUT/KernelHandoff.o" "$OUT/HalSerial.o" \
        "$OUT/BootInfo.o" "$OUT/Module.o" "$OUT/KernelModules.o" \
        "$OUT/HalCapability.o" "$OUT/HalVideoStub.o" "$OUT/Font.o" \
        "$OUT/PhysicalMemory.o" "$OUT/Device.o" "$OUT/VirtualMemory.o" \
        "$OUT/KernelMain.o"
    ;;
riscv|RiscV|RISCV)
    ARCH=RiscV
    OUT="$OUT_ROOT/RiscV"
    mkdir -p "$OUT"
    CC="$(pick_riscv_cc)"
    ARCH_CFLAGS=(-march=rv64imac_zicsr_zifencei -mabi=lp64 -mcmodel=medany)
    CFLAGS=("${COMMON_CFLAGS[@]}" "${ARCH_CFLAGS[@]}"
            -I"$SCRIPT_DIR/Hal/RiscV" -I"$SCRIPT_DIR/Hal/RiscV/Board/virt"
            -I"$SCRIPT_DIR/Hal/RiscV/Hal")
    build_common_objs "$CC" "$OUT" 1 "${CFLAGS[@]}"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/RiscV/KernelEntry.S" -o "$OUT/KernelEntry.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/RiscV/KernelHandoff.c" -o "$OUT/KernelHandoff.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/RiscV/SmpStub.c" -o "$OUT/SmpStub.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/RiscV/HalSerial.c" -o "$OUT/HalSerial.o"
    "$CC" -nostdlib -ffreestanding -no-pie "${ARCH_CFLAGS[@]}" \
        -Wl,-T,"$SCRIPT_DIR/Hal/RiscV/link.ld" \
        -o "$OUT/Kernel.elf" \
        "$OUT/KernelEntry.o" "$OUT/KernelHandoff.o" "$OUT/SmpStub.o" \
        "$OUT/HalSerial.o" \
        "$OUT/BootInfo.o" "$OUT/Module.o" "$OUT/KernelModules.o" \
        "$OUT/HalCapability.o" "$OUT/HalVideoStub.o" "$OUT/Font.o" \
        "$OUT/PhysicalMemory.o" "$OUT/Device.o" "$OUT/VirtualMemory.o" \
        "$OUT/KernelMain.o"
    ;;
*)
    echo "usage: $0 [x64|arm64|riscv] [SERIAL=0|1]" >&2
    exit 1
    ;;
esac

echo "=========================================="
echo "Kernel/$ARCH OK  → $OUT/Kernel.elf  (SERIAL=$TOY_SERIAL SCREEN_LOG=$TOY_SCREEN_LOG)"
echo "CC=$CC"
ls -lh "$OUT/Kernel.elf"
echo "=========================================="
