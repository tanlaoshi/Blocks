#!/usr/bin/env bash
# build-sdk.sh — 打出可离树分发的 BlocksSDK（x86_64）
# 产物：Build/BlocksSDK/ + Build/BlocksSDK.tar.gz
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK_SRC="$ROOT/Tools/Sdk"
OUT="$ROOT/Build/BlocksSDK"
TAR="$ROOT/Build/BlocksSDK.tar.gz"
ARCH="${1:-x86_64}"

if [[ "$ARCH" != "x86_64" ]]; then
    echo "build-sdk: 本刀只打 x86_64（收到 ARCH=$ARCH）" >&2
    exit 1
fi

echo "==> 确保 CRT / 用户库（ARCH=$ARCH）"
make -C "$ROOT" \
    User/crt/crt0.o \
    User/crt/syscall.o \
    User/Library/Blocks/libBlocks.a \
    User/Library/Graphics/libGraphics.a \
    User/Library/Ui/libUi.a \
    User/Library/Net/libNet.a \
    User/Library/FsUtil/libFsUtil.a

echo "==> 暂存 $OUT"
rm -rf "$OUT"
mkdir -p "$OUT/Include/Abi" "$OUT/Lib" "$OUT/Link" "$OUT/Docs"

cp -a "$ROOT/User/include/." "$OUT/Include/"
cp -f \
    "$ROOT/Include/Abi/SyscallABI.h" \
    "$ROOT/Include/Abi/TaskSnap.h" \
    "$ROOT/Include/Abi/GraphicsDamage.h" \
    "$ROOT/Include/Abi/Compiler.h" \
    "$OUT/Include/Abi/"

cp -f "$ROOT/User/crt/crt0.o" "$ROOT/User/crt/syscall.o" "$OUT/Lib/"
cp -f "$ROOT/User/Library/Blocks/libBlocks.a" "$OUT/Lib/"
cp -f "$ROOT/User/Library/Graphics/libGraphics.a" "$OUT/Lib/"
cp -f "$ROOT/User/Library/Ui/libUi.a" "$OUT/Lib/"
cp -f "$ROOT/User/Library/Net/libNet.a" "$OUT/Lib/"
cp -f "$ROOT/User/Library/FsUtil/libFsUtil.a" "$OUT/Lib/"
cp -f "$ROOT/User/user.ld" "$OUT/Link/user.ld"

cp -f "$SDK_SRC/VERSION" "$SDK_SRC/README.md" "$SDK_SRC/BlocksSdk.mk" \
    "$SDK_SRC/Makefile.template" "$OUT/"
cp -a "$SDK_SRC/Examples" "$OUT/"

# 可选：一页 API（缺文件不失败）
if [[ -f "$ROOT/../Archive/手册/API速查.md" ]]; then
    cp -f "$ROOT/../Archive/手册/API速查.md" "$OUT/Docs/"
fi

echo "==> 打包 $TAR"
tar -C "$ROOT/Build" -czf "$TAR" BlocksSDK

echo "OK  VERSION=$(cat "$OUT/VERSION")"
echo "    $OUT"
echo "    $TAR ($(du -h "$TAR" | awk '{print $1}'))"
echo "验收: tar xzf $TAR -C /tmp && make -C /tmp/BlocksSDK/Examples/Hello"
