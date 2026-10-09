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
    # Core 按模块表分目录；产物名仍用 basename.o（链接行不变）
    local f
    local base
    local -a cores=(
        BootInfo Modules Kernel
        Serial/Serial
        Memory/Memory Memory/PhysicalMemory
        VirtualMemory/VirtualMemory
        Driver/Driver Driver/Device
        Video/Video Video/Theme Video/ThemeCfg Video/FontTerminus10x18 Video/FontCjkDisk Video/Font Video/Utf8 Video/FontTtfLoad
        Video/FontTtfCache Video/Locale
        Cpu/Cpu
        USB/Usb
        FileSystem/FileSystem FileSystem/FatProbe FileSystem/FatVol FileSystem/FatAlloc
        FileSystem/FatDir FileSystem/FatFile FileSystem/FatMut
        Network/Network
        Gui/GuiLayout Gui/GuiDesktop Gui/GuiSettings Gui/GuiFiles Gui/GuiStart Gui/GuiWinPaint Gui/GuiWin Gui/Gui
        Scheduler/Scheduler
        Console/ElfLoad Console/Process Console/ShellCmd Console/ShellSys Console/Console
    )
    for f in "${cores[@]}"; do
        base="$(basename "$f")"
        "$cc" "${cflags[@]}" -c "$SCRIPT_DIR/Core/${f}.c" -o "$out/${base}.o"
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
    # K27：允许 SSE 的 TU（其余仍 general-regs-only）
    CFLAGS_FPU=("${COMMON_CFLAGS[@]}" -m64 -mno-red-zone -msse2 -mfpmath=sse
                -I"$SCRIPT_DIR/Hal/X64" -I"$BLOCKS_ROOT/Boot/BootPkg"
                -I"$SCRIPT_DIR/ThirdParty/stb")
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/X64/HalFpu.c" -o "$OUT/HalFpu.o"
    "$CC" "${CFLAGS_FPU[@]}" -c "$SCRIPT_DIR/Hal/X64/HalFpuSse.c" -o "$OUT/HalFpuSse.o"
    "$CC" "${CFLAGS_FPU[@]}" -c "$SCRIPT_DIR/Core/Video/FontTtfRaster.c" -o "$OUT/FontTtfRaster.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/X64/KernelEntry.S" -o "$OUT/KernelEntry.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/X64/KernelHandoff.c" -o "$OUT/KernelHandoff.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/X64/EarlyIdentity.c" -o "$OUT/EarlyIdentity.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/X64/PlatformStub.c" -o "$OUT/PlatformStub.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/X64/HalSerial.c" -o "$OUT/HalSerial.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/X64/HalBootFont.c" -o "$OUT/HalBootFont.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/X64/HalSerialGop.c" -o "$OUT/HalSerialGop.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/X64/HalVideo.c" -o "$OUT/HalVideo.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/X64/HalCpu.c" -o "$OUT/HalCpu.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/X64/HalCpuIsr.S" -o "$OUT/HalCpuIsr.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/X64/HalXhci.c" -o "$OUT/HalXhci.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/X64/HalPs2Kbd.c" -o "$OUT/HalPs2Kbd.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/X64/HalPs2Mouse.c" -o "$OUT/HalPs2Mouse.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/X64/HalVirtioBlk.c" -o "$OUT/HalVirtioBlk.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/X64/HalLapicTimer.c" -o "$OUT/HalLapicTimer.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/X64/HalVirtioNet.c" -o "$OUT/HalVirtioNet.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/X64/HalSyscall.c" -o "$OUT/HalSyscall.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/X64/HalSyscall.S" -o "$OUT/HalSyscallIsr.o"
    "$CC" -nostdlib -ffreestanding -no-pie \
        -Wl,--build-id=none \
        -Wl,-T,"$SCRIPT_DIR/Hal/X64/link.ld" \
        -o "$OUT/Kernel.elf" \
        "$OUT/KernelEntry.o" "$OUT/KernelHandoff.o" "$OUT/EarlyIdentity.o" \
        "$OUT/PlatformStub.o" "$OUT/HalSerial.o" "$OUT/HalBootFont.o" "$OUT/HalSerialGop.o" \
        "$OUT/HalVideo.o" "$OUT/HalCpu.o" "$OUT/HalCpuIsr.o" \
        "$OUT/HalXhci.o" "$OUT/HalPs2Kbd.o" "$OUT/HalPs2Mouse.o" \
        "$OUT/HalVirtioBlk.o" "$OUT/HalLapicTimer.o" "$OUT/HalVirtioNet.o" \
        "$OUT/HalSyscall.o" "$OUT/HalSyscallIsr.o" \
        "$OUT/HalFpu.o" "$OUT/HalFpuSse.o" \
        "$OUT/BootInfo.o" "$OUT/Modules.o" \
        "$OUT/Serial.o" "$OUT/Memory.o" "$OUT/PhysicalMemory.o" \
        "$OUT/VirtualMemory.o" "$OUT/Driver.o" "$OUT/Device.o" \
        "$OUT/Video.o" "$OUT/HalCapability.o" "$OUT/Theme.o" "$OUT/ThemeCfg.o" "$OUT/FontTerminus10x18.o" "$OUT/FontCjkDisk.o" "$OUT/Font.o" "$OUT/Utf8.o" \
        "$OUT/FontTtfLoad.o" "$OUT/FontTtfRaster.o" "$OUT/FontTtfCache.o" "$OUT/Locale.o" \
        "$OUT/Cpu.o" "$OUT/Usb.o" \
        "$OUT/FileSystem.o" "$OUT/FatProbe.o" "$OUT/FatVol.o" "$OUT/FatAlloc.o" "$OUT/FatDir.o" "$OUT/FatFile.o" "$OUT/FatMut.o" \
        "$OUT/ElfLoad.o" "$OUT/Process.o" \
        "$OUT/Network.o" "$OUT/GuiLayout.o" "$OUT/GuiDesktop.o" "$OUT/GuiSettings.o" "$OUT/GuiFiles.o" "$OUT/GuiStart.o" "$OUT/GuiWinPaint.o" "$OUT/GuiWin.o" "$OUT/Gui.o" \
        "$OUT/Scheduler.o" "$OUT/ShellCmd.o" "$OUT/ShellSys.o" "$OUT/Console.o" "$OUT/Kernel.o"
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
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/Common/HalFpuStub.c" -o "$OUT/HalFpu.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Core/Video/FontTtfRaster.c" -o "$OUT/FontTtfRaster.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/Arm64/KernelEntry.S" -o "$OUT/KernelEntry.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/Arm64/KernelHandoff.c" -o "$OUT/KernelHandoff.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/Arm64/HalSerial.c" -o "$OUT/HalSerial.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/Common/HalCpuStub.c" -o "$OUT/HalCpuStub.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/Common/HalXhciStub.c" -o "$OUT/HalXhciStub.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/Common/HalPs2KbdStub.c" -o "$OUT/HalPs2KbdStub.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/Common/HalPs2MouseStub.c" -o "$OUT/HalPs2MouseStub.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/Common/HalBlockStub.c" -o "$OUT/HalBlockStub.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/Common/HalTimerStub.c" -o "$OUT/HalTimerStub.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/Common/HalSyscallStub.c" -o "$OUT/HalSyscallStub.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/Common/HalNetStub.c" -o "$OUT/HalNetStub.o"
    "$CC" -nostdlib -ffreestanding -no-pie \
        -Wl,--build-id=none \
        -Wl,-T,"$SCRIPT_DIR/Hal/Arm64/link.ld" \
        -o "$OUT/Kernel.elf" \
        "$OUT/KernelEntry.o" "$OUT/KernelHandoff.o" "$OUT/HalSerial.o" \
        "$OUT/BootInfo.o" "$OUT/Modules.o" \
        "$OUT/Serial.o" "$OUT/Memory.o" "$OUT/PhysicalMemory.o" \
        "$OUT/VirtualMemory.o" "$OUT/Driver.o" "$OUT/Device.o" \
        "$OUT/Video.o" "$OUT/HalCapability.o" "$OUT/HalVideoStub.o" "$OUT/HalCpuStub.o" \
        "$OUT/HalXhciStub.o" "$OUT/HalPs2KbdStub.o" "$OUT/HalPs2MouseStub.o" \
        "$OUT/HalBlockStub.o" "$OUT/HalTimerStub.o" "$OUT/HalSyscallStub.o" \
        "$OUT/HalNetStub.o" \
        "$OUT/Theme.o" "$OUT/ThemeCfg.o" "$OUT/FontTerminus10x18.o" "$OUT/FontCjkDisk.o" "$OUT/Font.o" "$OUT/Utf8.o" \
        "$OUT/FontTtfLoad.o" "$OUT/FontTtfRaster.o" "$OUT/FontTtfCache.o" "$OUT/Locale.o" "$OUT/HalFpu.o" \
        "$OUT/Cpu.o" "$OUT/Usb.o" "$OUT/FileSystem.o" "$OUT/FatProbe.o" "$OUT/FatVol.o" "$OUT/FatAlloc.o" "$OUT/FatDir.o" "$OUT/FatFile.o" "$OUT/FatMut.o" \
        "$OUT/ElfLoad.o" "$OUT/Process.o" \
        "$OUT/Network.o" "$OUT/GuiLayout.o" "$OUT/GuiDesktop.o" "$OUT/GuiSettings.o" "$OUT/GuiFiles.o" "$OUT/GuiStart.o" "$OUT/GuiWinPaint.o" "$OUT/GuiWin.o" "$OUT/Gui.o" \
        "$OUT/Scheduler.o" "$OUT/ShellCmd.o" "$OUT/ShellSys.o" "$OUT/Console.o" "$OUT/Kernel.o"
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
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/Common/HalFpuStub.c" -o "$OUT/HalFpu.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Core/Video/FontTtfRaster.c" -o "$OUT/FontTtfRaster.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/RiscV/KernelEntry.S" -o "$OUT/KernelEntry.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/RiscV/KernelHandoff.c" -o "$OUT/KernelHandoff.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/RiscV/SmpStub.c" -o "$OUT/SmpStub.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/RiscV/HalSerial.c" -o "$OUT/HalSerial.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/Common/HalCpuStub.c" -o "$OUT/HalCpuStub.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/Common/HalXhciStub.c" -o "$OUT/HalXhciStub.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/Common/HalPs2KbdStub.c" -o "$OUT/HalPs2KbdStub.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/Common/HalPs2MouseStub.c" -o "$OUT/HalPs2MouseStub.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/Common/HalBlockStub.c" -o "$OUT/HalBlockStub.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/Common/HalTimerStub.c" -o "$OUT/HalTimerStub.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/Common/HalSyscallStub.c" -o "$OUT/HalSyscallStub.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/Common/HalNetStub.c" -o "$OUT/HalNetStub.o"
    "$CC" -nostdlib -ffreestanding -no-pie "${ARCH_CFLAGS[@]}" \
        -Wl,--build-id=none \
        -Wl,-T,"$SCRIPT_DIR/Hal/RiscV/link.ld" \
        -o "$OUT/Kernel.elf" \
        "$OUT/KernelEntry.o" "$OUT/KernelHandoff.o" "$OUT/SmpStub.o" \
        "$OUT/HalSerial.o" \
        "$OUT/BootInfo.o" "$OUT/Modules.o" \
        "$OUT/Serial.o" "$OUT/Memory.o" "$OUT/PhysicalMemory.o" \
        "$OUT/VirtualMemory.o" "$OUT/Driver.o" "$OUT/Device.o" \
        "$OUT/Video.o" "$OUT/HalCapability.o" "$OUT/HalVideoStub.o" "$OUT/HalCpuStub.o" \
        "$OUT/HalXhciStub.o" "$OUT/HalPs2KbdStub.o" "$OUT/HalPs2MouseStub.o" \
        "$OUT/HalBlockStub.o" "$OUT/HalTimerStub.o" "$OUT/HalSyscallStub.o" \
        "$OUT/HalNetStub.o" \
        "$OUT/Theme.o" "$OUT/ThemeCfg.o" "$OUT/FontTerminus10x18.o" "$OUT/FontCjkDisk.o" "$OUT/Font.o" "$OUT/Utf8.o" \
        "$OUT/FontTtfLoad.o" "$OUT/FontTtfRaster.o" "$OUT/FontTtfCache.o" "$OUT/Locale.o" "$OUT/HalFpu.o" \
        "$OUT/Cpu.o" "$OUT/Usb.o" "$OUT/FileSystem.o" "$OUT/FatProbe.o" "$OUT/FatVol.o" "$OUT/FatAlloc.o" "$OUT/FatDir.o" "$OUT/FatFile.o" "$OUT/FatMut.o" \
        "$OUT/ElfLoad.o" "$OUT/Process.o" \
        "$OUT/Network.o" "$OUT/GuiLayout.o" "$OUT/GuiDesktop.o" "$OUT/GuiSettings.o" "$OUT/GuiFiles.o" "$OUT/GuiStart.o" "$OUT/GuiWinPaint.o" "$OUT/GuiWin.o" "$OUT/Gui.o" \
        "$OUT/Scheduler.o" "$OUT/ShellCmd.o" "$OUT/ShellSys.o" "$OUT/Console.o" "$OUT/Kernel.o"
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
