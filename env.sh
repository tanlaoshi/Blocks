#!/bin/bash
# env.sh — Blocks 短名环境（须 source，对标 edksetup）
#
#   source ~/Blocks/env.sh          # 或：cd ~/Blocks && source ./env.sh
#
#   build                           # x64 Kernel（默认）
#   build arm64                     # QEMU virt（默认 BOARD=virt）
#   build arm64 BOARD=rpi-3b-plus LWIP=0
#   build arm64 BOARD=virt          # 切回课堂靶后再 run-virt-arm
#   build riscv BOARD=milk-v-duo-s
#   boot                            # Boot → BOOTX64.EFI
#   sync / sync usb|nuc|kernel
#   run / smoke                     # x64 QEMU
#   run-virt-arm --headless         # Arm64 virt（须先 build arm64）
#   run-virt-riscv
#   boards [arm64|riscv]            # 列出板包
#   prepare-pi-sd [OUT] [--dtb=…]   # rpi-3b-plus SD 摆盘目录（不插卡）
#
# 路径：BLOCKS_ROOT / KERNEL / RUNTIME / KERNEL_BUILD
# 实现在 Kernel/Scripts/；勿再写 cd Kernel && ./build.sh …
#
if [ "${BASH_SOURCE[0]}" = "$0" ]; then
    echo "error: 请 source 本脚本，例如: source ~/Blocks/env.sh" >&2
    exit 1
fi

_BlocksEnvDir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
_BlocksRootCand="$_BlocksEnvDir"
if [ ! -d "$_BlocksRootCand/Kernel" ] || [ ! -d "$_BlocksRootCand/Boot" ] || \
   [ ! -d "$_BlocksRootCand/Runtime" ]; then
    echo "error: 无法解析 Blocks 根（期望含 Boot/ Kernel/ Runtime/），从 $_BlocksEnvDir" >&2
    unset _BlocksEnvDir _BlocksRootCand
    return 1 2>/dev/null || exit 1
fi

export BLOCKS_ROOT="$_BlocksRootCand"
export KERNEL="$BLOCKS_ROOT/Kernel"
export RUNTIME="$BLOCKS_ROOT/Runtime"
export KERNEL_BUILD="$KERNEL/Build"
export BLOCKS_BOOT="$BLOCKS_ROOT/Boot"
export BLOCKS_SCRIPTS="$BLOCKS_ROOT/Kernel/Scripts"

if [ ! -d "$BLOCKS_SCRIPTS/lib" ]; then
    echo "error: 缺少 $BLOCKS_SCRIPTS/lib" >&2
    unset _BlocksEnvDir _BlocksRootCand
    return 1 2>/dev/null || exit 1
fi

# shellcheck source=Kernel/Scripts/lib/toyos-common.sh
. "$BLOCKS_SCRIPTS/lib/toyos-common.sh"

build() {
    (cd "$KERNEL" && ./build.sh "$@")
}

boot() {
    (cd "$BLOCKS_BOOT" && ./build.sh "$@")
}

sync() {
    case "${1:-}" in
        usb|nuc|kernel)
            bash "$BLOCKS_SCRIPTS/sync/sync.sh" "$@"
            ;;
        *)
            (cd "$RUNTIME" && ./sync.sh "$@")
            ;;
    esac
}

run() {
    local t0 t1
    t0=$(date +%s)
    (cd "$RUNTIME" && ./run.sh "$@")
    t1=$(date +%s)
    # 图形窗被关 / DISPLAY 异常时常见：印完横幅几秒内就回到 shell
    if [ "$((t1 - t0))" -le 3 ] && [ "$#" -eq 0 ]; then
        echo "run: QEMU 立刻退出了。先: run --kill ；再: run --headless （串口进 blocks>）" >&2
        echo "     或盯任务栏/其它桌面是否有 QEMU 窗被立刻关掉。" >&2
    fi
}

smoke() {
    (cd "$RUNTIME" && ./smoke-boot.sh "$@")
}

smoke-boot() {
    smoke "$@"
}

run-virt-arm() {
    bash "$BLOCKS_SCRIPTS/run/run-virt-arm.sh" "$@"
}

run-virt-riscv() {
    bash "$BLOCKS_SCRIPTS/run/run-virt-riscv.sh" "$@"
}

# 列出 HAL/<Arch>/Board 板包。用法：boards | boards arm64 | boards riscv
boards() {
    local Arch="${1:-arm64}"
    (cd "$KERNEL" && make ARCH="$Arch" boards)
}

# U-Boot 用扁平二进制。用法：kernel-bin arm64 BOARD=rpi-3b-plus
kernel-bin() {
    local Arch=arm64
    local Board=virt
    local A
    for A in "$@"; do
        case "$A" in
            arm64|aarch64) Arch=arm64 ;;
            riscv|riscv64) Arch=riscv ;;
            BOARD=*) Board="${A#BOARD=}" ;;
            *=*) ;;
            *)
                if [ -n "$A" ]; then
                    Arch="$A"
                fi
                ;;
        esac
    done
    (cd "$KERNEL" && make ARCH="$Arch" BOARD="$Board" LWIP=0 kernel-bin)
}

# rpi-3b-plus SD 摆盘目录（不插卡）。须先 build arm64 BOARD=rpi-3b-plus
prepare-pi-sd() {
    bash "$KERNEL/HAL/Arm64/Board/rpi-3b-plus/scripts/prepare-sd.sh" "$@"
}

# 暗号 CK：固件→bootfs；Kernel→BLOCKS。可选 --repart 整盘两 FAT32（仅 Pi）
ck-pi-sd() {
    bash "$KERNEL/HAL/Arm64/Board/rpi-3b-plus/scripts/ck-pi-sd.sh" "$@"
}
repart-pi-sd() {
    bash "$KERNEL/HAL/Arm64/Board/rpi-3b-plus/scripts/repart-pi-sd.sh" "$@"
}

# Duo：Kernel.elf → LABEL=BLOCKS（blocks-elf）。可选 --boot 刷新 boot.sd；禁止当 CK
ck-duo-sd() {
    bash "$KERNEL/HAL/RiscV/Board/milk-v-duo-s/scripts/ck-duo-sd.sh" "$@"
}

echo "BLOCKS_ROOT=$BLOCKS_ROOT KERNEL=$KERNEL RUNTIME=$RUNTIME KERNEL_BUILD=$KERNEL_BUILD"
echo "短名: build boot sync|sync usb run smoke run-virt-arm|riscv boards kernel-bin prepare-pi-sd ck-pi-sd ck-duo-sd repart-pi-sd"
unset _BlocksEnvDir _BlocksRootCand
