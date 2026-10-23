#!/bin/bash
# env.sh — Blocks 短名环境（须 source，对标 edksetup）
#
#   source ~/Blocks/env.sh          # 或：cd ~/Blocks && source ./env.sh
#
#   build                           # x64 Kernel（默认）
#   build arm64                     # QEMU virt（默认 BOARD=virt）
#   build arm64 BOARD=Raspberry/Pi3BPlus LWIP=0
#   build arm64 BOARD=virt          # 切回课堂靶后再 run-virt-arm
#   build riscv BOARD=milk-v-duo-s
#   boot                            # Boot → BOOTX64.EFI
#   sync / sync usb|nuc|kernel
#   run / smoke                     # x64 QEMU
#   run-virt-arm --headless         # Arm64 virt（须先 build arm64）
#   run-virt-riscv
#   boards [arm64|riscv]            # 列出板包
#
# 路径：BLOCKS_ROOT / KERNEL / RUNTIME / KERNEL_BUILD
# 实现仍在 Kernel/OpenBox/Scripts/；勿再写 cd Kernel && ./build.sh …
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
export BLOCKS_SCRIPTS="$BLOCKS_ROOT/Kernel/OpenBox/Scripts"

if [ ! -d "$BLOCKS_SCRIPTS/lib" ]; then
    echo "error: 缺少 $BLOCKS_SCRIPTS/lib" >&2
    unset _BlocksEnvDir _BlocksRootCand
    return 1 2>/dev/null || exit 1
fi

# shellcheck source=Kernel/OpenBox/Scripts/lib/toyos-common.sh
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
    (cd "$RUNTIME" && ./run.sh "$@")
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

# U-Boot 用扁平二进制。用法：kernel-bin arm64 BOARD=Raspberry/Pi3BPlus
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
    (cd "$KERNEL" && make ARCH="$Arch" BOARD="$Board" kernel-bin)
}

echo "BLOCKS_ROOT=$BLOCKS_ROOT KERNEL=$KERNEL RUNTIME=$RUNTIME KERNEL_BUILD=$KERNEL_BUILD"
echo "短名: build boot sync|sync usb run smoke run-virt-arm|riscv boards kernel-bin"
unset _BlocksEnvDir _BlocksRootCand
