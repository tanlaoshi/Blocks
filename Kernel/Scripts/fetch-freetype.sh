#!/bin/bash
# fetch-freetype.sh — 按固定版本抓取 FreeType 上游源码（gitignore，构建前按需取）
#
# 版本钉死：FreeType 2.13.2（上游 tag VER-2-13-2）
# 来源：https://github.com/freetype/freetype/archive/refs/tags/VER-2-13-2.tar.gz
# 许可：FreeType License / GPLv2 双许可（见树内 LICENSE.TXT）；只下源码，不入库。
#
# 两条校验，任一不符即失败（绝不静默换版本）：
#   1) tarball sha256
#   2) 解包树内容指纹（文件相对路径 + 内容；不含 mtime/mode）：
#        cd <树根> && find . -type f -print0 | LC_ALL=C sort -z | \
#            xargs -0 -r sha256sum | sha256sum
#      同时核对文件数。
#
# 用法（在 Kernel/ 下）：
#   Scripts/fetch-freetype.sh           # 缺则下载；已有且校验通过则跳过（幂等）
#   Scripts/fetch-freetype.sh --check   # 只校验现有树，不联网
#   Scripts/fetch-freetype.sh --force   # 丢弃现有树重下（校验失败时显式覆盖）
#   Scripts/fetch-freetype.sh --help
#
# 缺树时 x86_64 构建默认报错（Kernel/build.sh / Makefile），不会静默退 stb。
set -euo pipefail

FT_VERSION=2.13.2
FT_TAG=VER-2-13-2
FT_URL="https://github.com/freetype/freetype/archive/refs/tags/${FT_TAG}.tar.gz"
FT_TARBALL_SHA256=427201f5d5151670d05c1f5b45bef5dda1f2e7dd971ef54f0feaaa7ffd2ab90c
FT_TREE_SHA256=cb12af55da443b3af1e8c37aa701b13fec5424c9e279740da4c71b04d0ff8259
FT_TREE_FILES=730

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KERNEL="$(cd "$HERE/.." && pwd)"
DEST="$KERNEL/ThirdParty/freetype"
REL_DEST="ThirdParty/freetype"

MODE=ensure
FORCE=0

say() { echo "fetch-freetype: $*"; }
die() { echo "fetch-freetype: error: $*" >&2; exit 1; }

usage() {
    cat <<'EOF'
fetch-freetype.sh — 按固定版本抓取 FreeType 上游源码（2.13.2 / VER-2-13-2）

用法（在 Kernel/ 下）：
  Scripts/fetch-freetype.sh           # 缺则下载；已有且校验通过则跳过（幂等）
  Scripts/fetch-freetype.sh --check   # 只校验现有树，不联网
  Scripts/fetch-freetype.sh --force   # 丢弃现有树重下
  Scripts/fetch-freetype.sh --help

目标目录：Kernel/ThirdParty/freetype（.gitignore，不入库）
校验：tarball sha256 + 解包树内容指纹 + 文件数；不符即失败。
EOF
}

tree_hash() {
    ( cd "$1" && find . -type f -print0 | LC_ALL=C sort -z | \
        xargs -0 -r sha256sum | sha256sum | awk '{print $1}' )
}

tree_files() {
    ( cd "$1" && find . -type f -print0 | LC_ALL=C sort -z | \
        tr -cd '\0' | wc -c | tr -d ' ' )
}

version_of() {
    local H="$1/include/freetype/freetype.h" M m p
    [ -f "$H" ] || return 1
    M=$(awk '$1 == "#define" && $2 == "FREETYPE_MAJOR" {print $3; exit}' "$H")
    m=$(awk '$1 == "#define" && $2 == "FREETYPE_MINOR" {print $3; exit}' "$H")
    p=$(awk '$1 == "#define" && $2 == "FREETYPE_PATCH" {print $3; exit}' "$H")
    [ -n "$M" ] && [ -n "$m" ] && [ -n "$p" ] || return 1
    printf '%s.%s.%s\n' "$M" "$m" "$p"
}

# 校验一棵树：版本宏 + 内容指纹 + 文件数。成功返回 0，失败返回 1。
verify_tree() {
    local Dir="$1" Ver Hash Count
    [ -d "$Dir" ] || { say "未找到目录: $Dir"; return 1; }
    if ! Ver=$(version_of "$Dir"); then
        say "读不到 $Dir/include/freetype/freetype.h 里的版本宏"
        return 1
    fi
    if [ "$Ver" != "$FT_VERSION" ]; then
        say "版本不符: $Ver（期望 $FT_VERSION）"
        return 1
    fi
    Count=$(tree_files "$Dir")
    if [ "$Count" != "$FT_TREE_FILES" ]; then
        say "文件数不符: $Count（期望 $FT_TREE_FILES）"
        return 1
    fi
    Hash=$(tree_hash "$Dir")
    if [ "$Hash" != "$FT_TREE_SHA256" ]; then
        say "内容指纹不符: $Hash（期望 $FT_TREE_SHA256）"
        return 1
    fi
    say "校验通过: FreeType $Ver, $Count 文件, tree sha256 $Hash"
    return 0
}

download() {
    local Url="$1" Out="$2"
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL --retry 3 --connect-timeout 20 -o "$Out" "$Url"
    elif command -v wget >/dev/null 2>&1; then
        wget -q -O "$Out" "$Url"
    else
        die "需要 curl 或 wget 才能下载；也可手工放到 $REL_DEST（须与指纹一致）"
    fi
}

for Arg in "$@"; do
    case "$Arg" in
        --check) MODE=check ;;
        --force) FORCE=1 ;;
        --help|-h) usage; exit 0 ;;
        *) usage >&2; die "未知参数: $Arg" ;;
    esac
done

if [ "$MODE" = check ] && [ "$FORCE" = 1 ]; then
    die "--check 与 --force 不能同时使用"
fi

case "$DEST" in
    */ThirdParty/freetype) ;;
    *) die "内部路径异常，拒绝操作: $DEST" ;;
esac

if [ "$MODE" = check ]; then
    [ -d "$DEST" ] || die "缺少 $REL_DEST；先运行 Scripts/fetch-freetype.sh（本机需联网）"
    verify_tree "$DEST" || die "$REL_DEST 校验失败"
    exit 0
fi

# 已有树：能校验就跳过（幂等）；校验失败要求 --force，不覆盖本地改动。
if [ -d "$DEST" ] && [ "$FORCE" != 1 ]; then
    if verify_tree "$DEST"; then
        say "已就绪: $REL_DEST（未联网；--force 可重下）"
        exit 0
    fi
    die "现有 $REL_DEST 校验失败；确认丢弃重下: Scripts/fetch-freetype.sh --force"
fi
if [ -e "$DEST" ] && [ ! -d "$DEST" ]; then
    die "$REL_DEST 存在但不是目录；请先移走"
fi

command -v tar >/dev/null 2>&1 || die "需要 tar"
command -v sha256sum >/dev/null 2>&1 || die "需要 sha256sum（coreutils）"

TMP="$(mktemp -d "${TMPDIR:-/tmp}/fetch-freetype.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

say "下载 FreeType $FT_VERSION: $FT_URL"
download "$FT_URL" "$TMP/ft.tar.gz" || die "下载失败（检查网络/代理）；也可手工放置源码后跑 --check"

GOT_TAR_SHA=$(sha256sum "$TMP/ft.tar.gz" | awk '{print $1}')
if [ "$GOT_TAR_SHA" != "$FT_TARBALL_SHA256" ]; then
    die "tarball sha256 不符: 期望 $FT_TARBALL_SHA256，实际 $GOT_TAR_SHA（上游归档变了？不要跳过校验）"
fi
say "tarball sha256 校验通过: $GOT_TAR_SHA"

say "解包到临时目录"
mkdir -p "$TMP/src"
tar -xzf "$TMP/ft.tar.gz" -C "$TMP/src" --strip-components=1 || die "解包失败"

verify_tree "$TMP/src" || die "解包树校验失败（拒绝安装）"

mkdir -p "$KERNEL/ThirdParty"
if [ -e "$DEST" ]; then
    say "--force: 移除旧 $REL_DEST"
    rm -rf -- "$DEST"
fi
mv -- "$TMP/src" "$DEST"

say "安装完成: $DEST"
say "FreeType $FT_VERSION / $FT_TREE_FILES 文件 / tree sha256 $FT_TREE_SHA256"
