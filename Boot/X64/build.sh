#!/bin/bash
# Boot/X64/build.sh — OpenBox：本目录源码 + 同级 EDK2 工具包
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

# 用法: ./build.sh          # 关闭调试输出（默认）
#       ./build.sh DEBUG=1  # 打开 BootDbg 日志
DEBUG=0
for Arg in "$@"; do
    case "$Arg" in
        DEBUG=1|debug=1) DEBUG=1 ;;
        DEBUG=0|debug=0) DEBUG=0 ;;
    esac
done

EDK2_ROOT="$SCRIPT_DIR/EDK2"
if [ ! -f "$EDK2_ROOT/edksetup.sh" ] || [ ! -d "$EDK2_ROOT/MdePkg" ] || [ ! -d "$EDK2_ROOT/BaseTools" ]; then
    echo "error: 找不到裁剪 EDK2（需要 $EDK2_ROOT/{edksetup.sh,MdePkg,BaseTools}）" >&2
    exit 1
fi

# 包名 ToyBoot：链到本目录（与 EDK2 平级的源码树）
ln -sfn .. "$EDK2_ROOT/ToyBoot"

# OpenBox 根（…/OpenBox）与 Boot/Build/X64
OPENBOX_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BOOT_BUILD_DIR="$OPENBOX_ROOT/Boot/Build/X64"
mkdir -p "$BOOT_BUILD_DIR"

cd "$EDK2_ROOT"
unset EDK_TOOLS_PATH
if [ -d "$EDK2_ROOT/BaseTools" ]; then
    export EDK_TOOLS_PATH="$EDK2_ROOT/BaseTools"
fi
# shellcheck disable=SC1091
source edksetup.sh

if ! command -v build >/dev/null 2>&1; then
    echo "error: edksetup 后仍无 build（检查 $EDK2_ROOT/BaseTools/BinWrappers/PosixLike）" >&2
    echo "hint: 删掉过期 Conf/BuildEnv.sh 后重试；或 make -C \"\$EDK_TOOLS_PATH/Source/C\"" >&2
    exit 1
fi

command build -a X64 -p ToyBoot/Boot.dsc -t GCC -D TOY_BOOT_DEBUG="$DEBUG"

EFI_OUT="$EDK2_ROOT/Build/ToyBoot/DEBUG_GCC/X64/ToyBoot.efi"
if [ ! -f "$EFI_OUT" ]; then
    echo "Build failed: $EFI_OUT not found" >&2
    exit 1
fi

cp -f "$EFI_OUT" "$BOOT_BUILD_DIR/BOOTX64.EFI"
# 便于对照：也留一份裸名
cp -f "$EFI_OUT" "$BOOT_BUILD_DIR/ToyBoot.efi"

echo "=========================================="
echo "Build successful! TOY_BOOT_DEBUG=$DEBUG"
echo "EDK2_ROOT=$EDK2_ROOT"
echo "Installed: $BOOT_BUILD_DIR/BOOTX64.EFI"
ls -lh "$BOOT_BUILD_DIR/BOOTX64.EFI"
echo "=========================================="
