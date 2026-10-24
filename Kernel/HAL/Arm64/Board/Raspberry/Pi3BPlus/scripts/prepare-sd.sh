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
| Kernel.elf | 必有；推荐 bootelf |
| Kernel.bin | 可选扁平二进制；备选 go |
| $DTB_NAME | $([ -f "$OUT/$DTB_NAME" ] && echo "已拷入" || echo "缺：请自备后放入本目录，或 prepare-pi-sd --dtb=…") |

## U-Boot（推荐 bootelf，与 Duo S 命令行板同形）

EOF
    if [ -f "$OUT/$DTB_NAME" ]; then
        cat <<EOF
# 推荐：扁平二进制（2021.01 bootelf 易把入口搞成 0）
load mmc 0:1 0x80000 Kernel.bin
load mmc 0:1 \${fdt_addr_r} pi.dtb
go 0x80000
EOF
    else
        cat <<EOF
# 先把 pi.dtb（或 $DTB_NAME）放进本分区，再：
load mmc 0:1 0x80000 Kernel.bin
load mmc 0:1 \${fdt_addr_r} pi.dtb
go 0x80000
EOF
    fi
    cat <<'EOF'

提示地址（BoardConfig；以 printenv 为准）：
  链接/运行     0x00080000（Pi3BPlus link.ld）
  fdt_addr_r    常见 0x02400000
  pi.dtb        短名，内容同 bcm2710-rpi-3-b-plus.dtb

入口（AArch64）：x0 = DTB 物理地址（go 时可能是 argc，可后置）。

## 备选：Kernel.elf + bootelf

load mmc 0:1 0x20000000 Kernel.elf
load mmc 0:1 ${fdt_addr_r} pi.dtb
fdt addr ${fdt_addr_r}
bootelf -p 0x20000000

## 关于 booti

booti 需要 Linux Image 头。本柱交付 ELF / .bin，**不**强制 Image 封装。

## 验收

- 公司：本脚本退出 0 且 Kernel.elf 在 OUT/ 即过（rpi-2）
- 回家：拷 SD → 上电串口（rpi-3）；DTB memory（rpi-4）
EOF
} >"$OUT/README-SD.txt"

cp -f "$BOARD_DIR/NOTES.md" "$OUT/NOTES-board.md" 2>/dev/null || true

echo "prepare-sd: OUT=$OUT"
ls -la "$OUT"
echo "prepare-sd: 拷到 SD FAT 后按 README-SD.txt；真机手测属 rpi-3"
