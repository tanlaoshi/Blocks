#!/bin/bash
# build.sh — build.sh [<blocks|boot|sdk|all>] [<x86|arm64|riscv>] [KEY=VAL…]
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=lib/blocks-common.sh
. "$HERE/lib/blocks-common.sh"

ROOT="$(blocks_resolve_root "$0")"
blocks_load_config "$ROOT"

TARGET="${DEFAULT_TARGET:-blocks}"
ARCH_IN="${DEFAULT_ARCH:-x86}"
ARGS=()
GOT_TARGET=0
GOT_ARCH=0

for A in "$@"; do
    case "$A" in
        *=*)
            ARGS+=("$A")
            ;;
        blocks|sdk|all|kernel|boot)
            if [ "$GOT_TARGET" = 0 ]; then
                TARGET="$A"
                GOT_TARGET=1
            else
                ARGS+=("$A")
            fi
            ;;
        x86|x86_64|X64|x64|arm64|aarch64|AA64|riscv|riscv64|RISCV)
            if [ "$GOT_ARCH" = 0 ]; then
                ARCH_IN="$A"
                GOT_ARCH=1
            else
                ARGS+=("$A")
            fi
            ;;
        *)
            ARGS+=("$A")
            ;;
    esac
done

KARCH="$(blocks_arch_kernel "$ARCH_IN")"

build_blocks() {
    echo "==> Kernel ($KARCH) @ $ROOT/Kernel"
    if [ "${#ARGS[@]}" -gt 0 ]; then
        (cd "$ROOT/Kernel" && ./build.sh "$KARCH" "${ARGS[@]}")
    else
        (cd "$ROOT/Kernel" && ./build.sh "$KARCH")
    fi
}

build_boot() {
    echo "==> Boot @ $ROOT/Boot (EDK2_SRC=${EDK2_SRC:-auto})"
    export BLOCKS_ROOT="$ROOT"
    if [ -n "${EDK2_SRC:-}" ]; then
        export EDK2_SRC
    fi
    if [ "${#ARGS[@]}" -gt 0 ]; then
        (cd "$ROOT/Boot" && ./build.sh "${ARGS[@]}")
    else
        (cd "$ROOT/Boot" && ./build.sh)
    fi
}

build_sdk() {
    local Sdk="$ROOT/Kernel/Tools/build-sdk.sh"
    [ -f "$Sdk" ] || blocks_die "无 $Sdk"
    echo "==> SDK"
    if [ "${#ARGS[@]}" -gt 0 ]; then
        (cd "$ROOT/Kernel" && bash Tools/build-sdk.sh "${ARGS[@]}")
    else
        (cd "$ROOT/Kernel" && bash Tools/build-sdk.sh)
    fi
}

case "$TARGET" in
    blocks|kernel) build_blocks ;;
    boot) build_boot ;;
    sdk) build_sdk ;;
    all)
        build_blocks
        build_boot
        ;;
    *) blocks_die "未知目标: $TARGET（blocks|boot|sdk|all）" ;;
esac
