#!/bin/bash
# run.sh — run.sh [<x86|arm64|riscv>] [split|virt] …
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=../lib/blocks-common.sh
. "$HERE/../lib/blocks-common.sh"

ROOT="$(blocks_resolve_root "$0")"
blocks_load_config "$ROOT"
LIB="$(blocks_scripts_lib "$0")"

ARCH_IN="${DEFAULT_ARCH:-x86}"
MODE=""
ARGS=()
for A in "$@"; do
    case "$A" in
        x86|x86_64|X64|arm64|aarch64|riscv|riscv64) ARCH_IN="$A" ;;
        split|virt) MODE="$A" ;;
        *) ARGS+=("$A") ;;
    esac
done

KARCH="$(blocks_arch_kernel "$ARCH_IN")"
case "$KARCH" in
    x86_64)
        MODE="${MODE:-split}"
        echo "==> run $MODE (x86)"
        "$LIB/run-split.sh" "${ARGS[@]+"${ARGS[@]}"}"
        ;;
    arm64)
        echo "==> run virt (arm64)"
        "$LIB/run-virt-arm.sh" "${ARGS[@]+"${ARGS[@]}"}"
        ;;
    riscv)
        echo "==> run virt (riscv)"
        "$LIB/run-virt-riscv.sh" "${ARGS[@]+"${ARGS[@]}"}"
        ;;
    *) blocks_die "未知 arch: $ARCH_IN" ;;
esac
