#!/bin/bash
# deploy.sh — Duo S 内核自更新：build → mk-boot-sd.py → ksend BOOT:boot.sd
#
# 用法（不必拔卡，串口线接着就行）：
#   source ~/Blocks/env.sh
#   deploy.sh --port /dev/ttyUSB0
#   deploy.sh --port /dev/ttyUSB0 --no-build          # 用已有 ELF
#   deploy.sh --port /dev/ttyUSB0 --elf Kernel.elf --no-build
#   deploy.sh --port /dev/ttyUSB0 --dry-run           # 只握手看差量，不写卡
#
# 流程：
#   1) （默认）build riscv duo → Kernel/Build/DUO/HAL/RiscV/Kernel.elf
#   2) mk-boot-sd.py -k <ELF> -o scripts/boot.sd（FIT 内嵌核）
#   3) Tools/ksend.py --file boot.sd --path BOOT:boot.sd --no-reboot
#      板端：备份 boot.sd → boot.sd.bak（写前读回校验）→ 写 → 整文件 CRC
#      读回比对；不符从 .bak 回滚并打 KSEND RESTORED。失败绝不复位。
#   4) 只有 KSEND OK（板端已读回校验）才提示可复位。
#
# 本脚本刻意不自动复位：复位时机留给操作者（板上 reboot / 断电重上电）。
# 最坏情况：卡上 boot.sd 损坏 → 下次上电不进内核 → 读卡器重刷（约 1 分钟）。
#
# 禁止：对 Duo 跑 ck-pi-sd；不要删 fip.bin / boot.sd.vendor。
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
BOARD_DIR="$(cd "$HERE/.." && pwd)"
# milk-v-duo-s → Board → RiscV → HAL → Kernel → Blocks
BLOCKS_ROOT="$(cd "$BOARD_DIR/../../../../.." && pwd)"
BUILD_DIR="${BLOCKS_DUO_BUILD_DIR:-$BLOCKS_ROOT/Kernel/Build/DUO}"
if [ ! -d "$BUILD_DIR" ] && [ -f "$BLOCKS_ROOT/Kernel/Build/HAL/RiscV/Kernel.elf" ]; then
    BUILD_DIR="$BLOCKS_ROOT/Kernel/Build"
fi
ELF="${BLOCKS_KERNEL_ELF:-$BUILD_DIR/HAL/RiscV/Kernel.elf}"
PORT="${KSEND_PORT:-/dev/ttyUSB0}"
BAUD="${KSEND_BAUD:-115200}"
TARGET="BOOT:boot.sd"
OUT="$HERE/boot.sd"
DTB=""
DO_BUILD=1
DRY_RUN=0

usage() {
    sed -n '2,22p' "$0" | sed 's/^# \?//'
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
        -h|--help) usage; exit 0 ;;
        *) echo "error: unknown arg: $1（--help 看用法）" >&2; exit 2 ;;
    esac
done

if [ "$DO_BUILD" = 1 ]; then
    echo "deploy: build riscv duo"
    (cd "$BLOCKS_ROOT/Kernel" && ./build.sh riscv duo)
fi

if [ ! -f "$ELF" ]; then
    echo "error: 缺 $ELF（先: build riscv duo，或 --elf 指定）" >&2
    exit 1
fi

# 防误刷：BOARD=virt 的产物上电会卡在 Starting kernel …
STAMP="$BUILD_DIR/HAL/RiscV/.toy_board"
if [ -f "$STAMP" ]; then
    _Board="$(tr -d '\n' <"$STAMP" || true)"
    if [ "$_Board" != "milk-v-duo-s" ]; then
        echo "error: .toy_board='$_Board'（不是 milk-v-duo-s）→ 先: build riscv duo" >&2
        exit 1
    fi
fi
unset _Board

echo "deploy: ELF=$ELF"
if [ -n "$DTB" ]; then
    python3 "$HERE/mk-boot-sd.py" -k "$ELF" -d "$DTB" -o "$OUT"
else
    python3 "$HERE/mk-boot-sd.py" -k "$ELF" -o "$OUT"
fi

if [ ! -f "$OUT" ]; then
    echo "error: mk-boot-sd.py 没产出 $OUT" >&2
    exit 1
fi
echo "deploy: boot.sd $(stat -c '%s' "$OUT") B md5=$(md5sum "$OUT" | cut -d' ' -f1)"

KSEND="$BLOCKS_ROOT/Tools/ksend.py"
KSEND_ARGS=(--port "$PORT" --baud "$BAUD" --file "$OUT" --path "$TARGET" --no-reboot)
if [ "$DRY_RUN" = 1 ]; then
    KSEND_ARGS+=(--dry-run)
fi

if ! python3 "$KSEND" "${KSEND_ARGS[@]}"; then
    echo "deploy: FAILED — boot.sd 未通过校验；板子未复位。" >&2
    echo "deploy: 可重试本脚本；必要时用读卡器把 boot.sd.bak 改名回 boot.sd。" >&2
    exit 1
fi

if [ "$DRY_RUN" = 1 ]; then
    echo "deploy: dry-run — 未写卡（已握手并 abort，板子未复位）"
    exit 0
fi

echo "deploy: OK — BOOT:boot.sd 已写入并整文件读回 CRC 校验通过"
echo "deploy: 现在可以复位：板上执行 reboot，或断电重上电（本脚本不复位）"
