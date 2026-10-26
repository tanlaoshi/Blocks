#!/bin/bash
# prepare-sd.sh — 摆盘到目录（可再拷进 SD 的 FAT 分区）；不要求本机插卡
#
#   source ~/Blocks/env.sh
#   build arm64 BOARD=rpi-3b-plus LWIP=1
#   prepare-pi-sd [/path/to/out] [--dtb=/path/to/bcm2837-rpi-3-b-plus.dtb]
#   （日常刷卡：ck-pi-sd 会先调本脚本再写卡）
#
# 产出 OUT/ 内：Kernel.elf、Kernel.bin（若能 objcopy）、可选 DTB、README-SD.txt
# 公司验收：目录齐即可。真机上电属 rpi-3，勿把本脚本成功当成 ready。
#
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
BOARD_DIR="$(cd "$HERE/.." && pwd)"
# Board/rpi-3b-plus → … → Blocks（五级；勿多一级）
BLOCKS_ROOT="$(cd "$BOARD_DIR/../../../../.." && pwd)"
KERNEL="$BLOCKS_ROOT/Kernel"
HALDIR="$KERNEL/Build/HAL/Arm64"
ELF="$HALDIR/Kernel.elf"
OUT=""
DTB_SRC=""
FORCE_BIN=0

usage() {
    cat <<'EOF'
usage: prepare-sd.sh [OUT_DIR] [--dtb=PATH] [--elf=PATH] [--bin]

  OUT_DIR   默认：$BLOCKS_ROOT/Build/rpi-3b-plus-sd
  --dtb=    拷入的 bcm2837-rpi-3-b-plus.dtb（缺则只写说明，不假装有 DTB）
  --elf=    覆盖默认 Build/HAL/Arm64/Kernel.elf
  --bin     强制再跑 make kernel-bin（需交叉 objcopy）

须已：build arm64 BOARD=rpi-3b-plus（一板一编）。
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

OUT="${OUT:-$BLOCKS_ROOT/Build/rpi-3b-plus-sd}"

if [ ! -f "$ELF" ]; then
    echo "error: missing $ELF" >&2
    echo "  先: source ~/Blocks/env.sh && build arm64 BOARD=rpi-3b-plus LWIP=1" >&2
    exit 1
fi

STAMP=""
if [ -f "$HALDIR/.toy_board" ]; then
    STAMP="$(cat "$HALDIR/.toy_board")"
fi
if [ -n "$STAMP" ] && [ "$STAMP" != "rpi-3b-plus" ]; then
    echo "error: Build/HAL/Arm64 当前板是 '$STAMP'，不是 rpi-3b-plus" >&2
    echo "  一板一编：build arm64 BOARD=rpi-3b-plus LWIP=1" >&2
    exit 1
fi

mkdir -p "$OUT"
cp -f "$ELF" "$OUT/Kernel.elf"

BIN="$HALDIR/Kernel.bin"
if [ "$FORCE_BIN" = 1 ] || [ ! -f "$BIN" ]; then
    if (cd "$KERNEL" && make ARCH=arm64 BOARD=rpi-3b-plus LWIP=1 kernel-bin); then
        BIN="$HALDIR/Kernel.bin"
    else
        echo "warn: kernel-bin 失败（可稍后补）；仍摆 Kernel.elf" >&2
        BIN=""
    fi
fi
if [ -n "${BIN:-}" ] && [ -f "$BIN" ]; then
    cp -f "$BIN" "$OUT/Kernel.bin"
fi

# boot.scr：stock U-Boot distro_bootcmd 上电自动 source（勿再手敲）
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

# U-Boot：历程钉死 stock 2021.01（distro_bootcmd → 自动找 /boot.scr）。
# 勿用带 bootcmd=bootflow scan 的新包当 u-boot.bin（常停提示符要手敲）。
#
# rpi-10：stock 默认可执行环境里是 bootdelay=-1（禁止自动启动→每次停提示符）。
# 同长改成 -2：零等待自动 bootcmd，且不因 CoolTerm/串口噪声 abort（U-Boot 语义）。
# 原 stock 文件保持 -1 不动；只改写出的 u-boot.bin。
UBOOT_STOCK=""
for c in \
    "$OUT/u-boot-2021.01-stock.bin" \
    "$OUT/u-boot-2021.01.bin" \
    "$BOARD_DIR/firmware/u-boot-2021.01-stock.bin"; do
    if [ -f "$c" ]; then
        UBOOT_STOCK=$c
        break
    fi
done
if [ -n "$UBOOT_STOCK" ]; then
    if [ -f "$OUT/u-boot.bin" ] && ! cmp -s "$UBOOT_STOCK" "$OUT/u-boot.bin"; then
        # 保异名备份一次，避免误盖可对照包
        if [ ! -f "$OUT/u-boot-prev.bin" ]; then
            cp -f "$OUT/u-boot.bin" "$OUT/u-boot-prev.bin"
        fi
    fi
    cp -f "$UBOOT_STOCK" "$OUT/u-boot.bin"
    python3 - "$OUT/u-boot.bin" <<'PY'
import sys
from pathlib import Path
p = Path(sys.argv[1])
data = bytearray(p.read_bytes())
old, new = b"bootdelay=-1", b"bootdelay=-2"
n = data.count(old)
if n != 1:
    print(f"error: u-boot.bin bootdelay=-1 count={n} (want 1)", file=sys.stderr)
    sys.exit(1)
i = data.find(old)
data[i : i + len(old)] = new
p.write_bytes(data)
print("prepare-sd: u-boot.bin bootdelay=-1 → -2 (autoboot)")
PY
    echo "prepare-sd: u-boot.bin <- $(basename "$UBOOT_STOCK") (auto boot.scr)"
else
    echo "warn: 无 u-boot-2021.01-stock.bin；未改 u-boot.bin" >&2
fi
# FAT 上若留着旧 uboot.env 会整表覆盖默认环境（可能仍是 -1）；摆盘不部署该文件
rm -f "$OUT/uboot.env"

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

## 分区（对齐 X64：ESP/bootfs + BLOCKS）

- **bootfs**（FAT32 type 0x0c）：仅固件 — u-boot.bin / boot.scr / pi.dtb / config.txt（**无** Kernel.elf）
- **BLOCKS**（FAT32 type 0x0c）：Kernel.elf + BLOCKS.ID + Assets/Fonts + 根文件
- 整盘重做：\`ck-pi-sd --repart\` 或 \`repart-pi-sd /dev/sdX\`
- \`boot.scr\`：\`load mmc 0:2 … Kernel.elf\`（第二分区）

## 本目录文件（prepare 产出；CK 分流拷贝）

| 文件 | 去向 |
| --- | --- |
| Kernel.elf | → LABEL=BLOCKS |
| u-boot.bin | → bootfs（stock 2021.01；自动 boot.scr） |
| pi.dtb / boot.scr / boot.cmd / config.txt | → bootfs |

## U-Boot（上电自动）

\`config.txt\`：\`kernel=u-boot.bin\`。prepare 已把默认知 \`bootdelay=-1\` 改成 \`-2\` → 上电自动 \`distro_bootcmd\` → \`/boot.scr\`。  
**一般不用敲命令。** 卡上勿留旧 \`uboot.env\`。救砖 / 自动失败时再用 HDMI 键盘：

EOF
    cat <<'EOF'
load mmc 0:1 0x1000000 boot.scr
source 0x1000000
EOF
    cat <<'EOF'

成功标志：Starting application at 0x00080000，串口 * → + → Boot: …

排障编年：Archive/过程/排障/Pi3BPlus真机启动排障历程.md

## 禁止

- 把带 \`bootflow scan\` 的新 U-Boot 盖成 \`u-boot.bin\`（常停提示符）
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
