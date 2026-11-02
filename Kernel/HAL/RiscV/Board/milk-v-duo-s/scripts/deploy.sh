#!/bin/bash
# deploy.sh — Duo S 热更：默认 ksend BLOCKS:Kernel.elf（BootStub 方案）
#
# 用法：
#   source ~/Blocks/env.sh
#   deploy-duo --port /dev/ttyUSB0
#   deploy-duo --port /dev/ttyUSB0 --no-build
#   deploy-duo --port /dev/ttyUSB0 --refresh-stub   # 重打 stub FIT → BOOT:boot.sd
#   deploy-duo --port /dev/ttyUSB0 --embed-kernel   # 旧路径：整核嵌入 boot.sd
#
# 前置：卡上 boot.sd 已是 stub FIT（ck-duo-sd 或曾 --refresh-stub）。
# 禁止：对 Duo 跑 ck-pi-sd；不要删 fip.bin / boot.sd.vendor。
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
BOARD_DIR="$(cd "$HERE/.." && pwd)"
BLOCKS_ROOT="$(cd "$BOARD_DIR/../../../../.." && pwd)"
BUILD_DIR="${BLOCKS_DUO_BUILD_DIR:-$BLOCKS_ROOT/Kernel/Build/DUO}"
if [ ! -d "$BUILD_DIR" ] && [ -f "$BLOCKS_ROOT/Kernel/Build/HAL/RiscV/Kernel.elf" ]; then
    BUILD_DIR="$BLOCKS_ROOT/Kernel/Build"
fi
ELF="${BLOCKS_KERNEL_ELF:-$BUILD_DIR/HAL/RiscV/Kernel.elf}"
STUB_ELF="$BUILD_DIR/BootStub/BootStub.elf"
PORT="${KSEND_PORT:-/dev/ttyUSB0}"
BAUD="${KSEND_BAUD:-115200}"
OUT="$HERE/boot.sd"
DTB=""
DO_BUILD=1
DRY_RUN=0
REFRESH_STUB=0
EMBED_KERNEL=0

usage() {
    sed -n '2,14p' "$0" | sed 's/^# \?//'
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --port|-p) PORT="${2:?--port needs a device}"; shift 2 ;;
        --port=*) PORT="${1#--port=}"; shift ;;
        --baud) BAUD="${2:?--baud needs a number}"; shift 2 ;;
        --baud=*) BAUD="${1#--baud=}"; shift ;;
        --elf|-k) ELF="${2:?--elf needs a path}"; shift 2 ;;
        --elf=*) ELF="${1#--elf=}"; shift ;;
        --dtb) DTB="${2:?--dtb needs a path}"; shift 2 ;;
        --dtb=*) DTB="${1#--dtb=}"; shift ;;
        --out|-o) OUT="${2:?--out needs a path}"; shift 2 ;;
        --out=*) OUT="${1#--out=}"; shift ;;
        --no-build) DO_BUILD=0; shift ;;
        --dry-run) DRY_RUN=1; shift ;;
        --refresh-stub) REFRESH_STUB=1; shift ;;
        --embed-kernel) EMBED_KERNEL=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "error: unknown arg: $1（--help 看用法）" >&2; exit 2 ;;
    esac
done

if [ "$REFRESH_STUB" = 1 ] && [ "$EMBED_KERNEL" = 1 ]; then
    echo "error: --refresh-stub 与 --embed-kernel 互斥" >&2
    exit 2
fi

if [ "$DO_BUILD" = 1 ]; then
    echo "deploy: build riscv duo"
    (cd "$BLOCKS_ROOT/Kernel" && ./build.sh riscv duo)
fi

if [ ! -f "$ELF" ]; then
    echo "error: 缺 $ELF（先: build riscv duo，或 --elf 指定）" >&2
    exit 1
fi

STAMP="$BUILD_DIR/HAL/RiscV/.toy_board"
if [ -f "$STAMP" ]; then
    _Board="$(tr -d '\n' <"$STAMP" || true)"
    if [ "$_Board" != "milk-v-duo-s" ]; then
        echo "error: .toy_board='$_Board'（不是 milk-v-duo-s）→ 先: build riscv duo" >&2
        exit 1
    fi
fi
unset _Board

KSEND="$BLOCKS_ROOT/Tools/ksend.py"
MK="$HERE/mk-boot-sd.py"

if [ "$REFRESH_STUB" = 1 ] || [ "$EMBED_KERNEL" = 1 ]; then
    if [ "$REFRESH_STUB" = 1 ]; then
        echo "deploy: build BootStub"
        bash "$BOARD_DIR/BootStub/build.sh"
        if [ ! -f "$STUB_ELF" ]; then
            echo "error: 缺 $STUB_ELF" >&2
            exit 1
        fi
        echo "deploy: mk-boot-sd --stub"
        if [ -n "$DTB" ]; then
            python3 "$MK" --stub -k "$STUB_ELF" -d "$DTB" -o "$OUT"
        else
            python3 "$MK" --stub -k "$STUB_ELF" -o "$OUT"
        fi
    else
        echo "deploy: mk-boot-sd embed full kernel (legacy)"
        if [ -n "$DTB" ]; then
            python3 "$MK" -k "$ELF" -d "$DTB" -o "$OUT"
        else
            python3 "$MK" -k "$ELF" -o "$OUT"
        fi
    fi
    if [ ! -f "$OUT" ]; then
        echo "error: mk-boot-sd.py 没产出 $OUT" >&2
        exit 1
    fi
    echo "deploy: boot.sd $(stat -c '%s' "$OUT") B md5=$(md5sum "$OUT" | cut -d' ' -f1)"
    TARGET="BOOT:boot.sd"
    KSEND_ARGS=(--port "$PORT" --baud "$BAUD" --file "$OUT" --path "$TARGET" \
                --no-reboot --no-backup --ready-timeout 600)
else
    echo "deploy: ELF=$ELF → BLOCKS:Kernel.elf"
    TARGET="BLOCKS:Kernel.elf"
    KSEND_ARGS=(--port "$PORT" --baud "$BAUD" --file "$ELF" --path "$TARGET" \
                --no-reboot --ready-timeout 180)
fi

if [ "$DRY_RUN" = 1 ]; then
    KSEND_ARGS+=(--dry-run)
fi

if ! python3 "$KSEND" "${KSEND_ARGS[@]}"; then
    echo "deploy: FAILED — 未通过校验；板子未复位。" >&2
    exit 1
fi

if [ "$DRY_RUN" = 1 ]; then
    echo "deploy: dry-run — 未写卡"
    exit 0
fi

echo "deploy: OK — $TARGET 已写入"
echo "deploy: 复位：板上 reboot / 断电（本脚本不复位）"
if [ "$REFRESH_STUB" = 0 ] && [ "$EMBED_KERNEL" = 0 ]; then
    echo "deploy: 须已刷 stub FIT；缺则：deploy-duo --refresh-stub 或 ck-duo-sd"
fi
