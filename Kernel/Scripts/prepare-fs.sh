#!/bin/bash
# prepare-fs.sh — prepare-fs.sh [<x86|arm64|riscv>]
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=lib/blocks-common.sh
. "$HERE/lib/blocks-common.sh"

ROOT="$(blocks_resolve_root "$0")"
blocks_load_config "$ROOT"
LIB="$(blocks_scripts_lib "$0")"
ARCH_IN="${1:-${DEFAULT_ARCH:-x86}}"
KARCH="$(blocks_arch_kernel "$ARCH_IN")"

case "$KARCH" in
    x86_64)
        echo "==> prepare-rootfs (x86)"
        "$LIB/prepare-rootfs.sh"
        ;;
    arm64|riscv)
        echo "==> prepare-virt-rootfs ($KARCH)"
        export VIRT_MAKE_ARCH="$KARCH"
        "$LIB/prepare-virt-rootfs.sh"
        ;;
    *) blocks_die "prepare-fs: 未知 arch $ARCH_IN" ;;
esac
