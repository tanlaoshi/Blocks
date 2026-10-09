#!/bin/bash
#
# sync.sh — 把 Boot / Kernel 构建产物拷进 Runtime 双盘布局
#
set -eo pipefail
RUNTIME="$(cd "$(dirname "$0")" && pwd)"
BLOCKS="$(cd "$RUNTIME/.." && pwd)"

BOOT_EFI="${BOOT_EFI:-$BLOCKS/Boot/Build/BOOTX64.EFI}"
KERNEL_ELF="${KERNEL_ELF:-$BLOCKS/Kernel/Build/X64/Kernel.elf}"

mkdir -p "$RUNTIME/Esp/X64/EFI/BOOT" "$RUNTIME/RootFs/X64" "$RUNTIME/Fw"

if [ ! -f "$BOOT_EFI" ]; then
    echo "error: 缺少 $BOOT_EFI（先 cd Boot && ./build.sh）" >&2
    exit 1
fi
if [ ! -f "$KERNEL_ELF" ]; then
    echo "error: 缺少 $KERNEL_ELF（先 cd Kernel && ./build.sh x64）" >&2
    exit 1
fi

cp -f "$BOOT_EFI" "$RUNTIME/Esp/X64/EFI/BOOT/BOOTX64.EFI"
cp -f "$KERNEL_ELF" "$RUNTIME/RootFs/X64/Kernel.elf"

if [ ! -f "$RUNTIME/RootFs/X64/TOYOS.ID" ]; then
    printf 'Blocks root volume\n' > "$RUNTIME/RootFs/X64/TOYOS.ID"
fi

# K19：确保 RootFs 有 HELLO.ELF
if [ ! -f "$RUNTIME/RootFs/X64/HELLO.ELF" ] || [ -x "$BLOCKS/User/X64/build.sh" ]; then
    if [ -x "$BLOCKS/User/X64/build.sh" ]; then
        "$BLOCKS/User/X64/build.sh" "$RUNTIME/RootFs/X64/HELLO.ELF"
    fi
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
