#!/bin/bash
# env.sh — Blocks 短名环境（须 source，对标 edksetup）
#
#   source ~/Blocks/Kernel/OpenBox/Scripts/env.sh
#   build                 # Kernel
#   boot                  # Boot → BOOTX64.EFI
#   sync / run / smoke    # Runtime
#   run-virt-arm|riscv    # QEMU virt -kernel
#
if [ "${BASH_SOURCE[0]}" = "$0" ]; then
    echo "error: 请 source 本脚本，例如: source Kernel/OpenBox/Scripts/env.sh" >&2
    exit 1
fi

_BlocksEnvDir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Scripts → OpenBox → Kernel → Blocks
_BlocksRootCand="$(cd "$_BlocksEnvDir/../../.." && pwd)"
if [ ! -d "$_BlocksRootCand/Kernel" ] || [ ! -d "$_BlocksRootCand/Boot" ] || \
   [ ! -d "$_BlocksRootCand/Runtime" ]; then
    echo "error: 无法解析 Blocks 根（期望含 Boot/ Kernel/ Runtime/），从 $_BlocksEnvDir" >&2
    unset _BlocksEnvDir _BlocksRootCand
    return 1 2>/dev/null || exit 1
fi

export BLOCKS_ROOT="$_BlocksRootCand"
export BLOCKS_KERNEL="$BLOCKS_ROOT/Kernel"
export BLOCKS_BOOT="$BLOCKS_ROOT/Boot"
export BLOCKS_RUNTIME="$BLOCKS_ROOT/Runtime"
export BLOCKS_SCRIPTS="$_BlocksEnvDir"
# 兼容仍读 TOYOS_ROOT 的旧脚本（指向 Blocks 根）
export TOYOS_ROOT="$BLOCKS_ROOT"

# shellcheck source=lib/toyos-common.sh
. "$BLOCKS_SCRIPTS/lib/toyos-common.sh"

build() {
    (cd "$BLOCKS_KERNEL" && ./build.sh "$@")
}

boot() {
    (cd "$BLOCKS_BOOT" && ./build.sh "$@")
}

sync() {
    (cd "$BLOCKS_RUNTIME" && ./sync.sh "$@")
}

run() {
    (cd "$BLOCKS_RUNTIME" && ./run.sh "$@")
}

smoke() {
    (cd "$BLOCKS_RUNTIME" && ./smoke-boot.sh "$@")
}

smoke-boot() {
    smoke "$@"
}

run-virt-arm() {
    bash "$BLOCKS_SCRIPTS/run-virt-arm.sh" "$@"
}

run-virt-riscv() {
    bash "$BLOCKS_SCRIPTS/run-virt-riscv.sh" "$@"
}

echo "BLOCKS_ROOT=$BLOCKS_ROOT"
echo "短名: build boot sync run smoke|smoke-boot run-virt-arm run-virt-riscv"
unset _BlocksEnvDir _BlocksRootCand
