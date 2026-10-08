#!/bin/bash
#
# Boot/build.sh — UEFI 引导：BootPkg + EDK2（无软链进 EDK2）
#
# WORKSPACE = Boot/
# PACKAGES_PATH = Boot/ : Boot/EDK2/
# 包名 BootPkg/（真实目录，不再 EDK2/ToyBoot -> ..）
#
set -eo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

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

# 去掉历史软链（若还在）
rm -f "$EDK2_ROOT/ToyBoot" "$EDK2_ROOT/BootPkg"

BOOT_BUILD_DIR="$SCRIPT_DIR/Build"
mkdir -p "$BOOT_BUILD_DIR"
# 只用 EDK2/Conf，不在 Boot/ 下另开一份
rm -rf "$SCRIPT_DIR/Conf"

export WORKSPACE="$SCRIPT_DIR"
export PACKAGES_PATH="$SCRIPT_DIR:$EDK2_ROOT"
export EDK_TOOLS_PATH="$EDK2_ROOT/BaseTools"
export CONF_PATH="$EDK2_ROOT/Conf"
unset EDK_TOOLS_BIN || true

# shellcheck disable=SC1091
source "$EDK2_ROOT/edksetup.sh"

if ! command -v build >/dev/null 2>&1; then
    echo "error: edksetup 后仍无 build（检查 $EDK_TOOLS_PATH/BinWrappers/PosixLike）" >&2
    exit 1
fi

# GenFw 等 C 工具首次需编译（Bin/Linux-*）
if ! command -v GenFw >/dev/null 2>&1; then
    echo "Building BaseTools C (GenFw)…"
    make -C "$EDK2_ROOT/BaseTools/Source/C" -j"$(nproc 2>/dev/null || echo 2)"
fi

command build -a X64 -p BootPkg/Boot.dsc -t GCC -D TOY_BOOT_DEBUG="$DEBUG"

EFI_OUT="$WORKSPACE/Build/BootPkg/DEBUG_GCC/X64/BootPkg.efi"
if [ ! -f "$EFI_OUT" ]; then
    # 兼容 RELEASE 或旧产物名
    EFI_OUT="$(ls "$WORKSPACE"/Build/BootPkg/*/X64/*.efi 2>/dev/null | head -1 || true)"
fi
if [ -z "${EFI_OUT:-}" ] || [ ! -f "$EFI_OUT" ]; then
    echo "Build failed: BootPkg.efi not found under Build/BootPkg/" >&2
    exit 1
fi

cp -f "$EFI_OUT" "$BOOT_BUILD_DIR/BOOTX64.EFI"
cp -f "$EFI_OUT" "$BOOT_BUILD_DIR/BootPkg.efi"

echo "=========================================="
echo "Boot OK  TOY_BOOT_DEBUG=$DEBUG"
echo "WORKSPACE=$WORKSPACE"
echo "PACKAGES_PATH=$PACKAGES_PATH"
echo "Installed: $BOOT_BUILD_DIR/BOOTX64.EFI"
ls -lh "$BOOT_BUILD_DIR/BOOTX64.EFI"
echo "=========================================="
