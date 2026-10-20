#!/bin/bash
#
# run.sh — Blocks X64 双盘 QEMU（OVMF + ESP + RootFs）
#
# 【初学者】
#   disk0 = Esp/X64/      → BOOTX64.EFI（AHCI.0）
#   disk1 = RootFs/X64/   → TOYOS.ID + BLOCKS.ID + Kernel.elf + Assets（AHCI.1）
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
# 优先 QEMU_XRES/YRES；否则读 BLOCKS.DB mode=WxH；缺省 → 1440x900。
QEMU_XRES="${QEMU_XRES:-}"
QEMU_YRES="${QEMU_YRES:-}"
if [ -z "$QEMU_XRES" ] || [ -z "$QEMU_YRES" ]; then
    Db="RootFs/X64/BLOCKS.DB"
    Line=""
    if [ -f "$Db" ]; then
        # 末尾可能有空字节，不用 -a 时 grep 会当成二进制，mode= 读不出来。
        Line="$(grep -aE '^[[:space:]]*mode=' "$Db" | head -1 || true)"
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
MSC_ARGS=()
if [ "${USB_MSC:-0}" = 1 ]; then
    if [ ! -f Msc/BLOCKS.ID ] && [ ! -f Msc/TOYOS.ID ]; then
        echo "error: USB_MSC=1 needs Msc/BLOCKS.ID" >&2
        exit 1
    fi
    echo "  MSC   Msc/ (usb-storage)"
    MSC_ARGS=(
        -drive if=none,id=toymsc,format=raw,file=fat:rw:Msc
        -device usb-storage,drive=toymsc,bus=xhci.0
    )
fi
echo "=========================================="

# 双盘均走 AHCI：现网 x86 块驱动认 AHCI，不认 virtio-blk（旧 run-split 同款）。
# disk0=Esp（Boot）；disk1=RootFs（TOYOS.ID / Assets / Kernel.elf）。
# K52：qemu-xhci + usb-kbd + usb-tablet（GTK 绝对鼠）。
# 课用现代 virtio-net（驱动走 PCI caps）；disable-modern 会让认卡失败、托盘一直红。
# 注意：# 注释不能插在 \ 续行中间，否则后面参数全被吃掉。
exec qemu-system-x86_64 \
    -machine q35,accel=kvm:tcg \
    -cpu qemu64,+hypervisor \
    -m "$MEM" \
    -smp "$SMP" \
    -drive if=pflash,format=raw,readonly=on,file="$OVMF_CODE" \
    -drive if=pflash,format=raw,file=Fw/OVMF_VARS.fd \
    -device ich9-ahci,id=ahci \
    -drive if=none,id=toyesp,format=raw,file=fat:rw:Esp/X64 \
    -device ide-hd,drive=toyesp,bus=ahci.0,bootindex=0 \
    -drive if=none,id=toyroot,format=raw,file=fat:rw:RootFs/X64 \
    -device ide-hd,drive=toyroot,bus=ahci.1,bootindex=1 \
    -device VGA,edid=on,xres="${QEMU_XRES}",yres="${QEMU_YRES}" \
    -device virtio-net-pci,netdev=n0 \
    -netdev user,id=n0,hostfwd=tcp::15000-:5000 \
    -device qemu-xhci,id=xhci \
    -device usb-kbd,bus=xhci.0 \
    -device usb-tablet,bus=xhci.0 \
    "${MSC_ARGS[@]}" \
    "${DISPLAY_ARGS[@]}" \
    "${SERIAL_ARGS[@]}"
