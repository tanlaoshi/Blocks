#!/bin/bash
# QEMU virt riscv64 验收（PR-V6）— 入口在 ToyImage；内核 ../ToyKernel
# 自有 Boot：OpenSBI + -kernel；不是 run-split.sh / RiscVVirt EDK2。
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=toyos-common.sh
. "$SCRIPT_DIR/toyos-common.sh"
IMAGE_ROOT="$(toyos_image_root "$0")"
cd "$IMAGE_ROOT"
BOARD=virt
# shellcheck source=run-virt-common.sh
source "$SCRIPT_DIR/run-virt-common.sh"

VIRT_ARCH=riscv
VIRT_MAKE_ARCH=riscv
VIRT_HAL_ARCH=RiscV
export VIRT_MAKE_ARCH VIRT_HAL_ARCH
VIRT_ELF="${VIRT_ELF:-}"
VIRT_QEMU="${QEMU_RISCV64:-qemu-system-riscv64}"
VIRT_HELLO_PAT='ToyOS RiscV virt: hello'

toy_virt_main "$@"
