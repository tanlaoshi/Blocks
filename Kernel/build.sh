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
# X64 默认 LWIP=1（K41）；LWIP=0 可关。Arm/RiscV 无端口，编时强制 0。
SERIAL_ENABLE=1
SCREEN_LOG=0
HAVE_LWIP=1
for Arg in "$@"; do
    case "$Arg" in
        SERIAL=0|serial=0) SERIAL_ENABLE=0 ;;
        SERIAL=1|serial=1) SERIAL_ENABLE=1 ;;
        SCREEN_LOG=0|screen_log=0) SCREEN_LOG=0 ;;
        SCREEN_LOG=1|screen_log=1) SCREEN_LOG=1 ;;
        LWIP=0|lwip=0) HAVE_LWIP=0 ;;
        LWIP=1|lwip=1) HAVE_LWIP=1 ;;
    esac
done

COMMON_CFLAGS=(
    -ffreestanding -nostdlib -O2 -Wall -Wextra
    -fno-stack-protector -fno-builtin -fno-pie -fno-pic
    -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0
    -DBRINGUP=0
    -DSERIAL_ENABLE="$SERIAL_ENABLE"
    -DSCREEN_LOG="$SCREEN_LOG"
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

# Core 产物列表（镜像源码子目录：Build/<Arch>/Core/...）
CORE_OBJS=()

build_common_objs() {
    local cc="$1"
    shift
    local out="$1"
    shift
    local with_video_stub="$1"
    shift
    local -a cflags=("$@")
    # 方案 3：按 Core 子目录落 .o，跨目录同名不冲突
    local f
    local obj
    local -a cores=(
        BootInfo Modules Kernel
        Serial/Serial
        Memory/Memory Memory/PhysicalMemory
        VirtualMemory/VirtualMemory VirtualMemory/VirtualMemorySpace
        Driver/Driver Driver/Device
        Video/Video Video/Theme Video/ThemeConfiguration Video/FontTerminus10x18 Video/FontCjkDisk Video/Font Video/Utf8 Video/FontTtfLoad
        Video/FontTtfCache Video/Locale
        Cpu/Cpu
        USB/Usb
        FileSystem/FileSystem FileSystem/FatProbe FileSystem/FatVolume FileSystem/FatAllocate
        FileSystem/FatDirectory FileSystem/FatFile FileSystem/FatMutation FileSystem/Gpt FileSystem/Volume FileSystem/DataBase
        FileSystem/Store FileSystem/StoreCatalog FileSystem/StoreJob
        Network/Network Network/Ip Network/Ping Network/Udp Network/Tcp Network/Lwip Network/Configuration
        Gui/Layout Gui/Desktop Gui/Settings Gui/Files Gui/StoreUi Gui/Start Gui/WindowPaint Gui/Window Gui/Gui
        Scheduler/Scheduler
        Console/ElfLoader Console/Process Console/ProcessFork Console/SyscallFile Console/ShellCommand Console/ShellCommandDataBase Console/ShellCommandStore Console/ShellSystem Console/Console
    )
    CORE_OBJS=()
    for f in "${cores[@]}"; do
        obj="$out/Core/${f}.o"
        mkdir -p "$(dirname "$obj")"
        "$cc" "${cflags[@]}" -c "$SCRIPT_DIR/Core/${f}.c" -o "$obj"
        CORE_OBJS+=("$obj")
    done
    mkdir -p "$out/Hal/Common"
    "$cc" "${cflags[@]}" -c "$SCRIPT_DIR/Hal/Common/HalCapability.c" \
        -o "$out/Hal/Common/HalCapability.o"
    if [ "$with_video_stub" = 1 ]; then
        "$cc" "${cflags[@]}" -c "$SCRIPT_DIR/Hal/Common/HalVideoStub.c" \
            -o "$out/Hal/Common/HalVideoStub.o"
    fi
}

case "$ARCH" in
x64|X64)
    ARCH=X64
    OUT="$OUT_ROOT/X64"
    mkdir -p "$OUT" "$OUT/lwip"
    CC="$(pick_x64_cc)"
    LWIP_CFLAGS=()
    LWIP_OBJS=()
    if [ "$HAVE_LWIP" = "1" ]; then
        LWIP_CFLAGS=(-DHAVE_LWIP=1
            -I"$SCRIPT_DIR/ThirdParty/lwip/src/include"
            -I"$SCRIPT_DIR/Hal/X64/LwIp/include"
            -I"$SCRIPT_DIR/Hal/X64/LwIp")
    fi
    CFLAGS=("${COMMON_CFLAGS[@]}" -m64 -mno-red-zone -mgeneral-regs-only
            -I"$SCRIPT_DIR/Hal/X64" -I"$BLOCKS_ROOT/Boot/BootPkg"
            "${LWIP_CFLAGS[@]}")
    build_common_objs "$CC" "$OUT" 0 "${CFLAGS[@]}"
    mkdir -p "$OUT/Core/Video" "$OUT/Hal/X64"
    # K27：允许 SSE 的 TU（其余仍 general-regs-only）
    CFLAGS_FPU=("${COMMON_CFLAGS[@]}" -m64 -mno-red-zone -msse2 -mfpmath=sse
                -I"$SCRIPT_DIR/Hal/X64" -I"$BLOCKS_ROOT/Boot/BootPkg"
                -I"$SCRIPT_DIR/ThirdParty/stb")
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/X64/HalFpu.c" -o "$OUT/Hal/X64/HalFpu.o"
    "$CC" "${CFLAGS_FPU[@]}" -c "$SCRIPT_DIR/Hal/X64/HalFpuSse.c" -o "$OUT/Hal/X64/HalFpuSse.o"
    "$CC" "${CFLAGS_FPU[@]}" -c "$SCRIPT_DIR/Core/Video/FontTtfRaster.c" -o "$OUT/Core/Video/FontTtfRaster.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/X64/KernelEntry.S" -o "$OUT/Hal/X64/KernelEntry.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/X64/KernelHandoff.c" -o "$OUT/Hal/X64/KernelHandoff.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/X64/EarlyIdentity.c" -o "$OUT/Hal/X64/EarlyIdentity.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/X64/PlatformStub.c" -o "$OUT/Hal/X64/PlatformStub.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/X64/HalSerial.c" -o "$OUT/Hal/X64/HalSerial.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/X64/HalBootFont.c" -o "$OUT/Hal/X64/HalBootFont.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/X64/HalSerialGop.c" -o "$OUT/Hal/X64/HalSerialGop.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/X64/HalVideo.c" -o "$OUT/Hal/X64/HalVideo.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/X64/HalCpu.c" -o "$OUT/Hal/X64/HalCpu.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/X64/HalCpuIsr.S" -o "$OUT/Hal/X64/HalCpuIsr.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/X64/HalXhci.c" -o "$OUT/Hal/X64/HalXhci.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/X64/HalPs2Keyboard.c" -o "$OUT/Hal/X64/HalPs2Keyboard.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/X64/HalPs2Mouse.c" -o "$OUT/Hal/X64/HalPs2Mouse.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/X64/HalVirtioBlk.c" -o "$OUT/Hal/X64/HalVirtioBlk.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/X64/HalLapicTimer.c" -o "$OUT/Hal/X64/HalLapicTimer.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/X64/HalVirtioNet.c" -o "$OUT/Hal/X64/HalVirtioNet.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/X64/HalSyscall.c" -o "$OUT/Hal/X64/HalSyscall.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/X64/HalSyscall.S" -o "$OUT/Hal/X64/HalSyscallIsr.o"
    if [ "$HAVE_LWIP" = "1" ]; then
        LWIPDIR="$SCRIPT_DIR/ThirdParty/lwip/src"
        for f in init def inet_chksum ip mem memp netif pbuf raw stats sys \
                 tcp tcp_in tcp_out timeouts udp dns \
                 ipv4/etharp ipv4/icmp ipv4/ip4 ipv4/ip4_addr; do
            base="$(basename "$f")"
            "$CC" "${CFLAGS[@]}" -Wno-unused-parameter -c "$LWIPDIR/core/${f}.c" \
                -o "$OUT/lwip/${base}.o"
            LWIP_OBJS+=("$OUT/lwip/${base}.o")
        done
        "$CC" "${CFLAGS[@]}" -Wno-unused-parameter -c "$LWIPDIR/netif/ethernet.c" \
            -o "$OUT/lwip/ethernet.o"
        LWIP_OBJS+=("$OUT/lwip/ethernet.o")
        mkdir -p "$OUT/Hal/X64/LwIp"
        "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/X64/LwIp/LwIpNetif.c" -o "$OUT/Hal/X64/LwIp/LwIpNetif.o"
        "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/X64/LwIp/LwIpIcmp.c" -o "$OUT/Hal/X64/LwIp/LwIpIcmp.o"
        "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/X64/LwIp/LwIpSock.c" -o "$OUT/Hal/X64/LwIp/LwIpSock.o"
        LWIP_OBJS+=("$OUT/Hal/X64/LwIp/LwIpNetif.o" "$OUT/Hal/X64/LwIp/LwIpIcmp.o" "$OUT/Hal/X64/LwIp/LwIpSock.o")
    fi
    "$CC" -nostdlib -ffreestanding -no-pie \
        -Wl,--build-id=none \
        -Wl,-T,"$SCRIPT_DIR/Hal/X64/link.ld" \
        -o "$OUT/Kernel.elf" \
        "$OUT/Hal/X64/KernelEntry.o" "$OUT/Hal/X64/KernelHandoff.o" "$OUT/Hal/X64/EarlyIdentity.o" \
        "$OUT/Hal/X64/PlatformStub.o" "$OUT/Hal/X64/HalSerial.o" "$OUT/Hal/X64/HalBootFont.o" "$OUT/Hal/X64/HalSerialGop.o" \
        "$OUT/Hal/X64/HalVideo.o" "$OUT/Hal/X64/HalCpu.o" "$OUT/Hal/X64/HalCpuIsr.o" \
        "$OUT/Hal/X64/HalXhci.o" "$OUT/Hal/X64/HalPs2Keyboard.o" "$OUT/Hal/X64/HalPs2Mouse.o" \
        "$OUT/Hal/X64/HalVirtioBlk.o" "$OUT/Hal/X64/HalLapicTimer.o" "$OUT/Hal/X64/HalVirtioNet.o" \
        "$OUT/Hal/X64/HalSyscall.o" "$OUT/Hal/X64/HalSyscallIsr.o" \
        "$OUT/Hal/X64/HalFpu.o" "$OUT/Hal/X64/HalFpuSse.o" \
        "$OUT/Core/BootInfo.o" "$OUT/Core/Modules.o" \
        "$OUT/Core/Serial/Serial.o" "$OUT/Core/Memory/Memory.o" "$OUT/Core/Memory/PhysicalMemory.o" \
        "$OUT/Core/VirtualMemory/VirtualMemory.o" "$OUT/Core/VirtualMemory/VirtualMemorySpace.o" "$OUT/Core/Driver/Driver.o" "$OUT/Core/Driver/Device.o" \
        "$OUT/Core/Video/Video.o" "$OUT/Hal/Common/HalCapability.o" "$OUT/Core/Video/Theme.o" "$OUT/Core/Video/ThemeConfiguration.o" "$OUT/Core/Video/FontTerminus10x18.o" "$OUT/Core/Video/FontCjkDisk.o" "$OUT/Core/Video/Font.o" "$OUT/Core/Video/Utf8.o" \
        "$OUT/Core/Video/FontTtfLoad.o" "$OUT/Core/Video/FontTtfRaster.o" "$OUT/Core/Video/FontTtfCache.o" "$OUT/Core/Video/Locale.o" \
        "$OUT/Core/Cpu/Cpu.o" "$OUT/Core/USB/Usb.o" \
        "$OUT/Core/FileSystem/FileSystem.o" "$OUT/Core/FileSystem/FatProbe.o" "$OUT/Core/FileSystem/FatVolume.o" "$OUT/Core/FileSystem/FatAllocate.o" "$OUT/Core/FileSystem/FatDirectory.o" "$OUT/Core/FileSystem/FatFile.o" "$OUT/Core/FileSystem/FatMutation.o" "$OUT/Core/FileSystem/Gpt.o" "$OUT/Core/FileSystem/Volume.o" "$OUT/Core/FileSystem/DataBase.o" "$OUT/Core/FileSystem/Store.o" "$OUT/Core/FileSystem/StoreCatalog.o" "$OUT/Core/FileSystem/StoreJob.o" \
        "$OUT/Core/Console/ElfLoader.o" "$OUT/Core/Console/Process.o" "$OUT/Core/Console/ProcessFork.o" "$OUT/Core/Console/SyscallFile.o" \
        "$OUT/Core/Network/Network.o" "$OUT/Core/Network/Ip.o" "$OUT/Core/Network/Ping.o" "$OUT/Core/Network/Udp.o" "$OUT/Core/Network/Tcp.o" "$OUT/Core/Network/Lwip.o" "$OUT/Core/Network/Configuration.o" "$OUT/Core/Gui/Layout.o" "$OUT/Core/Gui/Desktop.o" "$OUT/Core/Gui/Settings.o" "$OUT/Core/Gui/Files.o" "$OUT/Core/Gui/StoreUi.o" "$OUT/Core/Gui/Start.o" "$OUT/Core/Gui/WindowPaint.o" "$OUT/Core/Gui/Window.o" "$OUT/Core/Gui/Gui.o" \
        "$OUT/Core/Scheduler/Scheduler.o" "$OUT/Core/Console/ShellCommand.o" "$OUT/Core/Console/ShellCommandDataBase.o" "$OUT/Core/Console/ShellCommandStore.o" "$OUT/Core/Console/ShellSystem.o" "$OUT/Core/Console/Console.o" "$OUT/Core/Kernel.o" \
        "${LWIP_OBJS[@]}"
    echo "Kernel/X64 LWIP=$HAVE_LWIP"
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
    mkdir -p "$OUT/Core/Video" "$OUT/Hal/Arm64" "$OUT/Hal/Common"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/Common/HalFpuStub.c" -o "$OUT/Hal/Common/HalFpu.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Core/Video/FontTtfRaster.c" -o "$OUT/Core/Video/FontTtfRaster.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/Arm64/KernelEntry.S" -o "$OUT/Hal/Arm64/KernelEntry.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/Arm64/KernelHandoff.c" -o "$OUT/Hal/Arm64/KernelHandoff.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/Arm64/HalSerial.c" -o "$OUT/Hal/Arm64/HalSerial.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/Common/HalCpuStub.c" -o "$OUT/Hal/Common/HalCpuStub.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/Common/HalXhciStub.c" -o "$OUT/Hal/Common/HalXhciStub.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/Common/HalPs2KeyboardStub.c" -o "$OUT/Hal/Common/HalPs2KeyboardStub.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/Common/HalPs2MouseStub.c" -o "$OUT/Hal/Common/HalPs2MouseStub.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/Common/HalBlockStub.c" -o "$OUT/Hal/Common/HalBlockStub.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/Common/HalTimerStub.c" -o "$OUT/Hal/Common/HalTimerStub.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/Common/HalSyscallStub.c" -o "$OUT/Hal/Common/HalSyscallStub.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/Common/HalNetStub.c" -o "$OUT/Hal/Common/HalNetStub.o"
    "$CC" -nostdlib -ffreestanding -no-pie \
        -Wl,--build-id=none \
        -Wl,-T,"$SCRIPT_DIR/Hal/Arm64/link.ld" \
        -o "$OUT/Kernel.elf" \
        "$OUT/Hal/Arm64/KernelEntry.o" "$OUT/Hal/Arm64/KernelHandoff.o" "$OUT/Hal/Arm64/HalSerial.o" \
        "$OUT/Core/BootInfo.o" "$OUT/Core/Modules.o" \
        "$OUT/Core/Serial/Serial.o" "$OUT/Core/Memory/Memory.o" "$OUT/Core/Memory/PhysicalMemory.o" \
        "$OUT/Core/VirtualMemory/VirtualMemory.o" "$OUT/Core/VirtualMemory/VirtualMemorySpace.o" "$OUT/Core/Driver/Driver.o" "$OUT/Core/Driver/Device.o" \
        "$OUT/Core/Video/Video.o" "$OUT/Hal/Common/HalCapability.o" "$OUT/Hal/Common/HalVideoStub.o" "$OUT/Hal/Common/HalCpuStub.o" \
        "$OUT/Hal/Common/HalXhciStub.o" "$OUT/Hal/Common/HalPs2KeyboardStub.o" "$OUT/Hal/Common/HalPs2MouseStub.o" \
        "$OUT/Hal/Common/HalBlockStub.o" "$OUT/Hal/Common/HalTimerStub.o" "$OUT/Hal/Common/HalSyscallStub.o" \
        "$OUT/Hal/Common/HalNetStub.o" \
        "$OUT/Core/Video/Theme.o" "$OUT/Core/Video/ThemeConfiguration.o" "$OUT/Core/Video/FontTerminus10x18.o" "$OUT/Core/Video/FontCjkDisk.o" "$OUT/Core/Video/Font.o" "$OUT/Core/Video/Utf8.o" \
        "$OUT/Core/Video/FontTtfLoad.o" "$OUT/Core/Video/FontTtfRaster.o" "$OUT/Core/Video/FontTtfCache.o" "$OUT/Core/Video/Locale.o" "$OUT/Hal/Common/HalFpu.o" \
        "$OUT/Core/Cpu/Cpu.o" "$OUT/Core/USB/Usb.o" "$OUT/Core/FileSystem/FileSystem.o" "$OUT/Core/FileSystem/FatProbe.o" "$OUT/Core/FileSystem/FatVolume.o" "$OUT/Core/FileSystem/FatAllocate.o" "$OUT/Core/FileSystem/FatDirectory.o" "$OUT/Core/FileSystem/FatFile.o" "$OUT/Core/FileSystem/FatMutation.o" "$OUT/Core/FileSystem/Gpt.o" "$OUT/Core/FileSystem/Volume.o" "$OUT/Core/FileSystem/DataBase.o" "$OUT/Core/FileSystem/Store.o" "$OUT/Core/FileSystem/StoreCatalog.o" "$OUT/Core/FileSystem/StoreJob.o" \
        "$OUT/Core/Console/ElfLoader.o" "$OUT/Core/Console/Process.o" "$OUT/Core/Console/ProcessFork.o" "$OUT/Core/Console/SyscallFile.o" \
        "$OUT/Core/Network/Network.o" "$OUT/Core/Network/Ip.o" "$OUT/Core/Network/Ping.o" "$OUT/Core/Network/Udp.o" "$OUT/Core/Network/Tcp.o" "$OUT/Core/Network/Lwip.o" "$OUT/Core/Network/Configuration.o" "$OUT/Core/Gui/Layout.o" "$OUT/Core/Gui/Desktop.o" "$OUT/Core/Gui/Settings.o" "$OUT/Core/Gui/Files.o" "$OUT/Core/Gui/StoreUi.o" "$OUT/Core/Gui/Start.o" "$OUT/Core/Gui/WindowPaint.o" "$OUT/Core/Gui/Window.o" "$OUT/Core/Gui/Gui.o" \
        "$OUT/Core/Scheduler/Scheduler.o" "$OUT/Core/Console/ShellCommand.o" "$OUT/Core/Console/ShellCommandDataBase.o" "$OUT/Core/Console/ShellCommandStore.o" "$OUT/Core/Console/ShellSystem.o" "$OUT/Core/Console/Console.o" "$OUT/Core/Kernel.o"
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
    mkdir -p "$OUT/Core/Video" "$OUT/Hal/RiscV" "$OUT/Hal/Common"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/Common/HalFpuStub.c" -o "$OUT/Hal/Common/HalFpu.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Core/Video/FontTtfRaster.c" -o "$OUT/Core/Video/FontTtfRaster.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/RiscV/KernelEntry.S" -o "$OUT/Hal/RiscV/KernelEntry.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/RiscV/KernelHandoff.c" -o "$OUT/Hal/RiscV/KernelHandoff.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/RiscV/SmpStub.c" -o "$OUT/Hal/RiscV/SmpStub.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/RiscV/HalSerial.c" -o "$OUT/Hal/RiscV/HalSerial.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/Common/HalCpuStub.c" -o "$OUT/Hal/Common/HalCpuStub.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/Common/HalXhciStub.c" -o "$OUT/Hal/Common/HalXhciStub.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/Common/HalPs2KeyboardStub.c" -o "$OUT/Hal/Common/HalPs2KeyboardStub.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/Common/HalPs2MouseStub.c" -o "$OUT/Hal/Common/HalPs2MouseStub.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/Common/HalBlockStub.c" -o "$OUT/Hal/Common/HalBlockStub.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/Common/HalTimerStub.c" -o "$OUT/Hal/Common/HalTimerStub.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/Common/HalSyscallStub.c" -o "$OUT/Hal/Common/HalSyscallStub.o"
    "$CC" "${CFLAGS[@]}" -c "$SCRIPT_DIR/Hal/Common/HalNetStub.c" -o "$OUT/Hal/Common/HalNetStub.o"
    "$CC" -nostdlib -ffreestanding -no-pie "${ARCH_CFLAGS[@]}" \
        -Wl,--build-id=none \
        -Wl,-T,"$SCRIPT_DIR/Hal/RiscV/link.ld" \
        -o "$OUT/Kernel.elf" \
        "$OUT/Hal/RiscV/KernelEntry.o" "$OUT/Hal/RiscV/KernelHandoff.o" "$OUT/Hal/RiscV/SmpStub.o" \
        "$OUT/Hal/RiscV/HalSerial.o" \
        "$OUT/Core/BootInfo.o" "$OUT/Core/Modules.o" \
        "$OUT/Core/Serial/Serial.o" "$OUT/Core/Memory/Memory.o" "$OUT/Core/Memory/PhysicalMemory.o" \
        "$OUT/Core/VirtualMemory/VirtualMemory.o" "$OUT/Core/VirtualMemory/VirtualMemorySpace.o" "$OUT/Core/Driver/Driver.o" "$OUT/Core/Driver/Device.o" \
        "$OUT/Core/Video/Video.o" "$OUT/Hal/Common/HalCapability.o" "$OUT/Hal/Common/HalVideoStub.o" "$OUT/Hal/Common/HalCpuStub.o" \
        "$OUT/Hal/Common/HalXhciStub.o" "$OUT/Hal/Common/HalPs2KeyboardStub.o" "$OUT/Hal/Common/HalPs2MouseStub.o" \
        "$OUT/Hal/Common/HalBlockStub.o" "$OUT/Hal/Common/HalTimerStub.o" "$OUT/Hal/Common/HalSyscallStub.o" \
        "$OUT/Hal/Common/HalNetStub.o" \
        "$OUT/Core/Video/Theme.o" "$OUT/Core/Video/ThemeConfiguration.o" "$OUT/Core/Video/FontTerminus10x18.o" "$OUT/Core/Video/FontCjkDisk.o" "$OUT/Core/Video/Font.o" "$OUT/Core/Video/Utf8.o" \
        "$OUT/Core/Video/FontTtfLoad.o" "$OUT/Core/Video/FontTtfRaster.o" "$OUT/Core/Video/FontTtfCache.o" "$OUT/Core/Video/Locale.o" "$OUT/Hal/Common/HalFpu.o" \
        "$OUT/Core/Cpu/Cpu.o" "$OUT/Core/USB/Usb.o" "$OUT/Core/FileSystem/FileSystem.o" "$OUT/Core/FileSystem/FatProbe.o" "$OUT/Core/FileSystem/FatVolume.o" "$OUT/Core/FileSystem/FatAllocate.o" "$OUT/Core/FileSystem/FatDirectory.o" "$OUT/Core/FileSystem/FatFile.o" "$OUT/Core/FileSystem/FatMutation.o" "$OUT/Core/FileSystem/Gpt.o" "$OUT/Core/FileSystem/Volume.o" "$OUT/Core/FileSystem/DataBase.o" "$OUT/Core/FileSystem/Store.o" "$OUT/Core/FileSystem/StoreCatalog.o" "$OUT/Core/FileSystem/StoreJob.o" \
        "$OUT/Core/Console/ElfLoader.o" "$OUT/Core/Console/Process.o" "$OUT/Core/Console/ProcessFork.o" "$OUT/Core/Console/SyscallFile.o" \
        "$OUT/Core/Network/Network.o" "$OUT/Core/Network/Ip.o" "$OUT/Core/Network/Ping.o" "$OUT/Core/Network/Udp.o" "$OUT/Core/Network/Tcp.o" "$OUT/Core/Network/Lwip.o" "$OUT/Core/Network/Configuration.o" "$OUT/Core/Gui/Layout.o" "$OUT/Core/Gui/Desktop.o" "$OUT/Core/Gui/Settings.o" "$OUT/Core/Gui/Files.o" "$OUT/Core/Gui/StoreUi.o" "$OUT/Core/Gui/Start.o" "$OUT/Core/Gui/WindowPaint.o" "$OUT/Core/Gui/Window.o" "$OUT/Core/Gui/Gui.o" \
        "$OUT/Core/Scheduler/Scheduler.o" "$OUT/Core/Console/ShellCommand.o" "$OUT/Core/Console/ShellCommandDataBase.o" "$OUT/Core/Console/ShellCommandStore.o" "$OUT/Core/Console/ShellSystem.o" "$OUT/Core/Console/Console.o" "$OUT/Core/Kernel.o"
    ;;
*)
    echo "usage: $0 [x64|arm64|riscv] [SERIAL=0|1]" >&2
    exit 1
    ;;
esac

echo "=========================================="
echo "Kernel/$ARCH OK  → $OUT/Kernel.elf  (SERIAL=$SERIAL_ENABLE SCREEN_LOG=$SCREEN_LOG)"
echo "CC=$CC"
ls -lh "$OUT/Kernel.elf"
echo "=========================================="
