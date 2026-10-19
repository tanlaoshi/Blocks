#!/bin/bash
#
# sync.sh — 把 Boot / Kernel 构建产物拷进 Runtime 双盘布局
#
# 架构：Blocks Boot + 现网能力核（Kernel 树已换血）
#
set -eo pipefail
RUNTIME="$(cd "$(dirname "$0")" && pwd)"
BLOCKS="$(cd "$RUNTIME/.." && pwd)"

BOOT_EFI="${BOOT_EFI:-$BLOCKS/Boot/Build/BOOTX64.EFI}"
# 现网 Makefile：Build/CodeA-HAL/X64/Kernel.elf
KERNEL_ELF="${KERNEL_ELF:-$BLOCKS/Kernel/Build/CodeA-HAL/X64/Kernel.elf}"
if [ ! -f "$KERNEL_ELF" ] && [ -f "$BLOCKS/Kernel/Build/HAL/X64/Kernel.elf" ]; then
    KERNEL_ELF="$BLOCKS/Kernel/Build/HAL/X64/Kernel.elf"
fi

mkdir -p "$RUNTIME/Esp/X64/EFI/BOOT" "$RUNTIME/RootFs/X64" "$RUNTIME/Fw"

if [ ! -f "$BOOT_EFI" ]; then
    echo "error: 缺少 $BOOT_EFI（先 cd Boot && ./build.sh）" >&2
    exit 1
fi
if [ ! -f "$KERNEL_ELF" ]; then
    echo "error: 缺少 $KERNEL_ELF（先 cd Kernel && ./build.sh）" >&2
    exit 1
fi

cp -f "$BOOT_EFI" "$RUNTIME/Esp/X64/EFI/BOOT/BOOTX64.EFI"
cp -f "$KERNEL_ELF" "$RUNTIME/RootFs/X64/Kernel.elf"

if [ ! -f "$RUNTIME/RootFs/X64/BLOCKS.ID" ]; then
    printf 'Blocks root volume\n' > "$RUNTIME/RootFs/X64/BLOCKS.ID"
fi

# TTF：缺则从旁挂种子 / 卷根补进 Assets/Fonts（核侧路径）
mkdir -p "$RUNTIME/RootFs/X64/Assets/Fonts"
if [ ! -f "$RUNTIME/RootFs/X64/Assets/Fonts/CJK.TTF" ]; then
    for Cjk in \
        "$RUNTIME/Assets/Fonts/CJK.TTF" \
        "$RUNTIME/RootFs/X64/CJK.TTF" \
        "$BLOCKS/Kernel/Tools/Fonts/CJK.TTF"
    do
        if [ -f "$Cjk" ]; then
            cp -f "$Cjk" "$RUNTIME/RootFs/X64/Assets/Fonts/CJK.TTF"
            break
        fi
    done
fi

if [ ! -f "$RUNTIME/Fw/OVMF_VARS.fd.clean" ]; then
    if [ -f /usr/share/OVMF/OVMF_VARS_4M.fd ]; then
        cp -f /usr/share/OVMF/OVMF_VARS_4M.fd "$RUNTIME/Fw/OVMF_VARS.fd.clean"
    else
        echo "error: 需要 OVMF_VARS（/usr/share/OVMF/OVMF_VARS_4M.fd）" >&2
        exit 1
    fi
fi

echo "sync OK:"
echo "  Esp  ← $BOOT_EFI"
echo "  Root ← $KERNEL_ELF"
