#!/bin/bash
# QEMU virt aarch64 验收（PR-V6）— 入口在 ToyImage；内核 ../ToyKernel
# 自有 Boot：-kernel + DTB loader + ramfb/virtio；不是 run-split.sh / AAVMF。
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=blocks-common.sh
. "$SCRIPT_DIR/blocks-common.sh"
IMAGE_ROOT="$(blocks_image_root "$0")"
cd "$IMAGE_ROOT"
BOARD=virt
# shellcheck source=run-virt-common.sh
source "$SCRIPT_DIR/run-virt-common.sh"

VIRT_ARCH=arm64
VIRT_MAKE_ARCH=arm64
VIRT_HAL_ARCH=Arm64
export VIRT_MAKE_ARCH VIRT_HAL_ARCH
VIRT_ELF="${VIRT_ELF:-}"
VIRT_QEMU="${QEMU_AARCH64:-qemu-system-aarch64}"
VIRT_HELLO_PAT='Blocks Arm64 virt: hello'

toy_virt_main "$@"
