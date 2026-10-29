#!/bin/bash
#
# Boot/build.sh — 编出 BOOTX64.EFI
#
# WORKSPACE = Boot/
# PACKAGES_PATH = Boot/ : Boot/EDK2/
# 应用包：Boot/BootPkg/
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

# 避免 EDK2 根下误放同名包目录干扰 PACKAGES_PATH
rm -f "$EDK2_ROOT/ToyBoot" "$EDK2_ROOT/BootPkg"

BOOT_BUILD_DIR="$SCRIPT_DIR/Build"
mkdir -p "$BOOT_BUILD_DIR"
# Conf 只使用 EDK2/Conf
rm -rf "$SCRIPT_DIR/Conf"

export WORKSPACE="$SCRIPT_DIR"
export PACKAGES_PATH="$SCRIPT_DIR:$EDK2_ROOT"
export EDK_TOOLS_PATH="$EDK2_ROOT/BaseTools"
export CONF_PATH="$EDK2_ROOT/Conf"
unset EDK_TOOLS_BIN || true

BOOT_CFG="$SCRIPT_DIR/BootPkg/UefiBootConfig.h"
KERN_CFG="$(cd "$SCRIPT_DIR/.." && pwd)/Kernel/BringUp/X64/UefiBootConfig.h"
if [ ! -f "$KERN_CFG" ]; then
    echo "error: 缺少内核交接头 $KERN_CFG" >&2
    exit 1
fi
# 布局必须一致；断言方言可不同（Boot=EDK2 _Static_assert，Kernel=BLOCKS_* 垫片，见 c99-dialect）。
uefi_boot_config_norm() {
    # 去断言方言与空行，只比结构体/宏布局
    sed -E \
        -e '/^#include "Compiler\.h"/d' \
        -e '/_Static_assert/d' \
        -e '/BLOCKS_STATIC_ASSERT/d' \
        -e '/^#if defined\(__GNUC__\)$/,/^#endif$/d' \
        -e '/与 Boot 副本布局钉死/d' \
        -e 's/Boot 跟 EDK2：可用 _Static_assert \/ __builtin_offsetof；内核侧走 C99 垫片。/DIALECT/' \
        -e 's/内核侧 C99 垫片；Boot 副本跟 EDK2（_Static_assert），不收进 c99-dialect。/DIALECT/' \
        -e '/^[[:space:]]*$/d' \
        "$1"
}
if ! diff -q <(uefi_boot_config_norm "$BOOT_CFG") <(uefi_boot_config_norm "$KERN_CFG") >/dev/null; then
    echo "error: UEFI_BOOT_CONFIG 双份布局不一致（已忽略断言方言）：" >&2
    echo "  $BOOT_CFG" >&2
    echo "  $KERN_CFG" >&2
    diff -u <(uefi_boot_config_norm "$BOOT_CFG") <(uefi_boot_config_norm "$KERN_CFG") >&2 || true
    exit 1
fi

# shellcheck disable=SC1091
# edksetup.sh 会读 $@：调脚本时若带 DEBUG=1，会被当成它的选项而打 usage 退出
set --
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

command build -a X64 -p BootPkg/Boot.dsc -t GCC -D BOOT_DEBUG="$DEBUG"

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
# sync usb/nuc 从 Runtime/Esp 取 BOOTX64；勿只留在 Boot/Build 导致 U 盘仍跑旧 EFI
RUNTIME_EFI="$(cd "$SCRIPT_DIR/.." && pwd)/Runtime/Esp/X64/EFI/BOOT/BOOTX64.EFI"
mkdir -p "$(dirname "$RUNTIME_EFI")"
cp -f "$EFI_OUT" "$RUNTIME_EFI"

echo "=========================================="
echo "Boot OK  BOOT_DEBUG=$DEBUG"
echo "WORKSPACE=$WORKSPACE"
echo "PACKAGES_PATH=$PACKAGES_PATH"
echo "Installed: $BOOT_BUILD_DIR/BOOTX64.EFI"
echo "Runtime : $RUNTIME_EFI"
ls -lh "$BOOT_BUILD_DIR/BOOTX64.EFI" "$RUNTIME_EFI"
echo "=========================================="
