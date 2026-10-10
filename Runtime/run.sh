#!/bin/bash
#
# run.sh — Blocks X64 双盘 QEMU（OVMF + ESP + RootFs）
#
# 【初学者】
#   disk0 = Esp/X64/      → BOOTX64.EFI
#   disk1 = RootFs/X64/   → BLOCKS.ID + Kernel.elf（Boot 优先从此盘读核）
#
# 用法：
#   ./run.sh                 # 默认同步产物后启动（有显示则 gtk）
#   ./run.sh --headless      # 无窗口，串口在终端
#   ./run.sh --no-sync       # 不拷贝 Boot/Kernel
#   ./run.sh --clean-nvram   # 重置 OVMF 变量盘
#   ./run.sh --kill          # 杀掉残留 qemu-system-x86_64
#
set -eo pipefail
RUNTIME="$(cd "$(dirname "$0")" && pwd)"
cd "$RUNTIME"

HEADLESS=0
DO_SYNC=1
CLEAN_NVRAM=0
MEM="${BLOCKS_MEM:-1024M}"
SMP="${BLOCKS_SMP:-1}"

for Arg in "$@"; do
    case "$Arg" in
        --headless|-n) HEADLESS=1 ;;
        --no-sync) DO_SYNC=0 ;;
        --clean-nvram) CLEAN_NVRAM=1 ;;
        --kill)
            pkill -9 -f qemu-system-x86_64 2>/dev/null || true
            echo "killed qemu-system-x86_64 (if any)"
            exit 0
            ;;
        -h|--help)
            sed -n '2,20p' "$0"
            exit 0
            ;;
        *)
            echo "unknown option: $Arg" >&2
            exit 1
            ;;
    esac
done

if [ "$DO_SYNC" = 1 ]; then
    "$RUNTIME/sync.sh"
fi

OVMF_CODE="${OVMF_CODE:-/usr/share/OVMF/OVMF_CODE_4M.fd}"
if [ ! -f "$OVMF_CODE" ]; then
    echo "error: 找不到 OVMF_CODE（$OVMF_CODE）" >&2
    exit 1
fi
if [ ! -f Fw/OVMF_VARS.fd.clean ]; then
    echo "error: 缺少 Fw/OVMF_VARS.fd.clean（再跑 ./sync.sh）" >&2
    exit 1
fi
if [ "$CLEAN_NVRAM" = 1 ] || [ ! -f Fw/OVMF_VARS.fd ]; then
    cp -f Fw/OVMF_VARS.fd.clean Fw/OVMF_VARS.fd
fi

if [ ! -f Esp/X64/EFI/BOOT/BOOTX64.EFI ] || [ ! -f RootFs/X64/Kernel.elf ]; then
    echo "error: 缺 BOOTX64.EFI 或 Kernel.elf（./sync.sh）" >&2
    exit 1
fi

DISPLAY_ARGS=(-display gtk,zoom-to-fit=off)
SERIAL_ARGS=(-serial stdio)
if [ "$HEADLESS" = 1 ]; then
    DISPLAY_ARGS=(-display none)
fi
# 无 DISPLAY 时强制无窗口
if [ -z "${DISPLAY:-}" ] && [ "$HEADLESS" = 0 ]; then
    DISPLAY_ARGS=(-display none)
    HEADLESS=1
fi

# VGA EDID：固件 GOP 初模；VM 内勿 SetMode（见 Boot Video.c），改分辨率须重起本脚本。
# 优先 QEMU_XRES/YRES；否则读 THEME.CFG mode=WxH；缺省/auto → 1440x900（32px 字更宽松）。
QEMU_XRES="${QEMU_XRES:-}"
QEMU_YRES="${QEMU_YRES:-}"
if [ -z "$QEMU_XRES" ] || [ -z "$QEMU_YRES" ]; then
    Cfg="RootFs/X64/THEME.CFG"
    Line=""
    if [ -f "$Cfg" ]; then
        Line="$(grep -E '^[[:space:]]*mode=' "$Cfg" | head -1 || true)"
    fi
    W="$(printf '%s' "$Line" | sed -n 's/.*mode=\([0-9][0-9]*\)[xX]\([0-9][0-9]*\).*/\1/p')"
    H="$(printf '%s' "$Line" | sed -n 's/.*mode=\([0-9][0-9]*\)[xX]\([0-9][0-9]*\).*/\2/p')"
    if [ -n "$W" ] && [ -n "$H" ]; then
        QEMU_XRES="$W"
        QEMU_YRES="$H"
    else
        QEMU_XRES=1440
        QEMU_YRES=900
    fi
fi

echo "=========================================="
echo "Blocks Runtime QEMU"
echo "  OVMF  $OVMF_CODE"
echo "  ESP   Esp/X64"
echo "  Root  RootFs/X64"
echo "  mem=$MEM smp=$SMP headless=$HEADLESS"
echo "  VGA   ${QEMU_XRES}x${QEMU_YRES}"
echo "=========================================="

# 鼠走 PS/2（K17）。勿挂裸 qemu-xhci：现网有 usb-tablet+XHCI-HID；
# Blocks 尚无 HID，挂 xhci 会让 GTK 指针走 USB，客人 PS/2 半死不活。
# ESP=IDE（Boot）；Root=virtio-blk legacy（K16 内核读 FAT）；勿 ide 双 unit
exec qemu-system-x86_64 \
    -machine q35,accel=kvm:tcg \
    -cpu qemu64 \
    -m "$MEM" \
    -smp "$SMP" \
    -drive if=pflash,format=raw,readonly=on,file="$OVMF_CODE" \
    -drive if=pflash,format=raw,file=Fw/OVMF_VARS.fd \
    -drive if=none,id=toyesp,format=raw,file=fat:rw:Esp/X64 \
    -device ide-hd,drive=toyesp,bus=ide.0,bootindex=0 \
    -drive if=none,id=toyroot,format=raw,file=fat:rw:RootFs/X64 \
    -device virtio-blk-pci,drive=toyroot,disable-modern=on,bootindex=1 \
    -drive if=none,id=toyespk,format=raw,file=fat:rw:Esp/X64 \
    -device virtio-blk-pci,drive=toyespk,disable-modern=on \
    -device VGA,edid=on,xres="${QEMU_XRES}",yres="${QEMU_YRES}" \
    -device virtio-net-pci,netdev=n0,disable-modern=on \
    -netdev user,id=n0,hostfwd=tcp::15000-:5000 \
    "${DISPLAY_ARGS[@]}" \
    "${SERIAL_ARGS[@]}"
