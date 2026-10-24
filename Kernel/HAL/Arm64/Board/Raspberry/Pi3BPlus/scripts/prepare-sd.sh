#!/bin/bash
# prepare-sd.sh — 摆盘到目录（可再拷进 SD 的 FAT 分区）；不要求本机插卡
#
#   source ~/Blocks/env.sh
#   build arm64 BOARD=Raspberry/Pi3BPlus LWIP=0
#   prepare-pi-sd [/path/to/out] [--dtb=/path/to/bcm2837-rpi-3-b-plus.dtb]
#
# 产出 OUT/ 内：Kernel.elf、Kernel.bin（若能 objcopy）、可选 DTB、README-SD.txt
# 公司验收：目录齐即可。真机上电属 rpi-3，勿把本脚本成功当成 ready。
#
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
BOARD_DIR="$(cd "$HERE/.." && pwd)"
# …/HAL/Arm64/Board/Raspberry/Pi3BPlus → Blocks 根
BLOCKS_ROOT="$(cd "$BOARD_DIR/../../../../../.." && pwd)"
KERNEL="$BLOCKS_ROOT/Kernel"
HALDIR="$KERNEL/Build/HAL/Arm64"
ELF="$HALDIR/Kernel.elf"
OUT=""
DTB_SRC=""
FORCE_BIN=0

usage() {
    cat <<'EOF'
usage: prepare-sd.sh [OUT_DIR] [--dtb=PATH] [--elf=PATH] [--bin]

  OUT_DIR   默认：$BLOCKS_ROOT/Build/Pi3BPlus-sd
  --dtb=    拷入的 bcm2837-rpi-3-b-plus.dtb（缺则只写说明，不假装有 DTB）
  --elf=    覆盖默认 Build/HAL/Arm64/Kernel.elf
  --bin     强制再跑 make kernel-bin（需交叉 objcopy）

须已：build arm64 BOARD=Raspberry/Pi3BPlus（一板一编）。
EOF
}

for A in "$@"; do
    case "$A" in
        -h|--help) usage; exit 0 ;;
        --dtb=*) DTB_SRC="${A#--dtb=}" ;;
        --elf=*) ELF="${A#--elf=}" ;;
        --bin) FORCE_BIN=1 ;;
        -*)
            echo "error: unknown option: $A" >&2
            usage >&2
            exit 1
            ;;
        *)
            if [ -z "$OUT" ]; then
                OUT="$A"
            else
                echo "error: unexpected arg: $A" >&2
                exit 1
            fi
            ;;
    esac
done

OUT="${OUT:-$BLOCKS_ROOT/Build/Pi3BPlus-sd}"

if [ ! -f "$ELF" ]; then
    echo "error: missing $ELF" >&2
    echo "  先: source ~/Blocks/env.sh && build arm64 BOARD=Raspberry/Pi3BPlus LWIP=0" >&2
    exit 1
fi

STAMP=""
if [ -f "$HALDIR/.toy_board" ]; then
    STAMP="$(cat "$HALDIR/.toy_board")"
fi
if [ -n "$STAMP" ] && [ "$STAMP" != "Raspberry/Pi3BPlus" ]; then
    echo "error: Build/HAL/Arm64 当前板是 '$STAMP'，不是 Raspberry/Pi3BPlus" >&2
    echo "  一板一编：build arm64 BOARD=Raspberry/Pi3BPlus LWIP=0" >&2
    exit 1
fi

mkdir -p "$OUT"
cp -f "$ELF" "$OUT/Kernel.elf"

BIN="$HALDIR/Kernel.bin"
if [ "$FORCE_BIN" = 1 ] || [ ! -f "$BIN" ]; then
    if (cd "$KERNEL" && make ARCH=arm64 BOARD=Raspberry/Pi3BPlus LWIP=0 kernel-bin); then
        BIN="$HALDIR/Kernel.bin"
    else
        echo "warn: kernel-bin 失败（可稍后补）；仍摆 Kernel.elf" >&2
        BIN=""
    fi
fi
if [ -n "${BIN:-}" ] && [ -f "$BIN" ]; then
    cp -f "$BIN" "$OUT/Kernel.bin"
fi

# boot.scr：少打字；串口丢字时用 HDMI 键盘敲两行即可
cp -f "$BOARD_DIR/scripts/boot.cmd" "$OUT/boot.cmd"
MKIMAGE=""
for c in mkimage "$BLOCKS_ROOT/Build/u-boot-src/u-boot-2021.01/tools/mkimage"; do
    if [ -x "$c" ] || command -v "$c" >/dev/null 2>&1; then
        MKIMAGE=$c
        break
    fi
done
if [ -n "$MKIMAGE" ]; then
    "$MKIMAGE" -A arm64 -O linux -T script -C none -n 'blocks-pi3' \
        -d "$OUT/boot.cmd" "$OUT/boot.scr" >/dev/null
else
    echo "warn: 无 mkimage，跳过 boot.scr（可稍后补）" >&2
fi

DTB_NAME="bcm2837-rpi-3-b-plus.dtb"
if [ -n "$DTB_SRC" ]; then
    if [ ! -f "$DTB_SRC" ]; then
        echo "error: --dtb 不是文件: $DTB_SRC" >&2
        exit 1
    fi
    cp -f "$DTB_SRC" "$OUT/$DTB_NAME"
fi

{
    cat <<EOF
# Raspberry Pi 3B+ — Blocks SD 摆盘说明（rpi-2）
#
# 本目录由 prepare-sd.sh 生成，可整份拷到 SD 第一分区（FAT）。
# 厂商/社区 U-Boot **不改源码**；变量以板上 printenv 为准。
#
# 生成时间: $(date -Iseconds)
# Kernel.elf: $ELF
# .toy_board: ${STAMP:-unknown}
#
# !!! 脚本成功 ≠ 真机 ready（rpi-3 串口手测）!!!

## 分区建议

- 第一分区：FAT32，放本目录文件（及你自备的 U-Boot / 固件，若尚未有 boot 链）
- 本脚本**不**写入 bootcode.bin / start.elf / U-Boot 二进制（请用发行版或官方固件）

## 文件

| 文件 | 说明 |
| --- | --- |
| Kernel.elf | 必用 |
| pi.dtb | 短名 DTB |
| boot.scr | **推荐**：两行启动（见下） |
| boot.cmd | 脚本源码 |

## U-Boot（推荐：少打字）

串口输入常丢字 → **用 HDMI 键盘敲命令**，CoolTerm 只看输出。

EOF
    cat <<'EOF'
load mmc 0:1 0x1000000 boot.scr
source 0x1000000
EOF
    cat <<'EOF'

成功标志：Starting application at 0x00080000，串口 * → + → Boot: …

排障编年：Documents/过程/排障/Pi3BPlus真机启动排障历程.md

## 禁止

- 在丢字的串口上敲长命令（易变成 lamm/otelf 等残句）
- load Kernel.elf 到 0x80000 再 bootelf（Starting at 0x0）

## 关于 booti

booti 需要 Linux Image 头。本柱交付 ELF / .bin，**不**强制 Image 封装。

## 验收

- 公司：本脚本退出 0 且 Kernel.elf 在 OUT/ 即过（rpi-2）
- 回家：拷 SD → 上电串口（rpi-3）；DTB memory（rpi-4）
EOF
} >"$OUT/README-SD.txt"
cp -f "$OUT/README-SD.txt" "$OUT/README-SD.md"

cp -f "$BOARD_DIR/NOTES.md" "$OUT/NOTES-board.md" 2>/dev/null || true

echo "prepare-sd: OUT=$OUT"
ls -la "$OUT"
echo "prepare-sd: 拷到 SD FAT 后按 README-SD.txt；真机手测属 rpi-3"
