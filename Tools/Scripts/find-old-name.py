#!/usr/bin/env python3
"""扫描仓库里残留的旧名字（忽略大小写），并按用途分类。

为什么要分类：
  字面量  在引号里，通常是卷名/文件名/用户可见文案 —— 改错会起不来，必须同步改盘上文件
  注释    代码里的说明，改了不影响运行
  文档    .md/.txt，多数是历史记录，建议保留原文（那时确实叫这个名字）

第三方目录、构建产物、二进制单独归入「已排除」，不混进命中数。

  Tools/Scripts/find-old-name.py --term 旧名                       # 汇总 + 分类
  Tools/Scripts/find-old-name.py --term 旧名 --detail              # 逐行列出
  Tools/Scripts/find-old-name.py --term 旧名 --only 字面量          # 只列功能性命中
  Tools/Scripts/find-old-name.py --term 旧名 --replace 新名 --fix   # 就地替换（不改文件名）

改文件名不自动做，只把需要 mv 的清单打出来。
"""
import argparse
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]

# 第三方 / 构建产物 / 工具链：不算自研，单独统计
EXCLUDE_PREFIXES = (
    ".git",
    "Boot/EDK2",
    "Boot/Build",
    "Kernel/Build",
    "Kernel/ThirdParty",
    "Tools/xpack-aarch64-none-elf-gcc-13.2.1-1.1",
    "Tools/xpack-riscv-none-elf-gcc-13.2.0-2",
)

BINARY_SUFFIXES = {
    ".elf", ".bin", ".o", ".a", ".so", ".img", ".ttf", ".otf", ".woff", ".woff2",
    ".png", ".jpg", ".jpeg", ".gif", ".webp", ".ico", ".pdf", ".zst", ".gz", ".xz",
    ".zip", ".7z", ".pyc", ".dtb",
    # .DB/.sqlite 不按后缀排除：BLOCKS.DB 是文本 KV，靠 NUL 检测区分
}

CODE_SUFFIXES = {
    ".c", ".h", ".s", ".S", ".py", ".sh", ".mk", ".cmake", ".ld", ".exp", ".bat",
    ".yml", ".yaml", ".json", ".toml", ".cfg", ".defconfig",
}
DOC_SUFFIXES = {".md", ".txt", ".rst"}

LITERAL = "字面量"
COMMENT = "注释"
DOC = "文档"
OTHER = "其他"
SHORT = {LITERAL: "L", COMMENT: "C", DOC: "D", OTHER: "?"}

SIZE_LIMIT = 4 * 1024 * 1024


def is_excluded(relative_path):
    text = relative_path.as_posix()
    for prefix in EXCLUDE_PREFIXES:
        if text == prefix or text.startswith(prefix + "/"):
            return True
    return False


def read_text(path):
    """能当文本读就返回内容，二进制返回 None。"""
    if path.suffix.lower() in BINARY_SUFFIXES:
        return None
    try:
        if path.stat().st_size > SIZE_LIMIT:
            return None
        raw = path.read_bytes()
    except OSError:
        return None
    if b"\x00" in raw[:8192]:
        return None
    return raw.decode("utf-8", errors="replace")


def classify(path, line, start):
    """判定这处命中是字面量、注释还是其他。"""
    if path.suffix.lower() in DOC_SUFFIXES:
        return DOC

    prefix = line[:start]
    if prefix.count('"') % 2 == 1 or prefix.count("'") % 2 == 1:
        return LITERAL

    for marker in ("//", "/*", "#"):
        index = prefix.find(marker)
        if index >= 0 and prefix.count('"', index) % 2 == 0:
            return COMMENT
    return OTHER


def region_of(relative_path):
    parts = relative_path.parts
    return parts[0] if len(parts) > 1 else "(根目录)"


def replace_term(text, term, replacement):
    """按原大小写形态替换：全大写→全大写、首字母大写→首字母大写、其余→小写。"""
    def convert(match):
        found = match.group(0)
        if found.isupper():
            return replacement.upper()
        if found[0].isupper():
            return replacement[:1].upper() + replacement[1:].lower()
        return replacement.lower()

    return re.sub(re.escape(term), convert, text, flags=re.IGNORECASE)


def main():
    parser = argparse.ArgumentParser(description="扫描残留旧名字（忽略大小写）")
    parser.add_argument("--term", required=True, help="要查找的旧名字（忽略大小写）")
    parser.add_argument("--replace", default=None, help="--fix 时的替换词")
    parser.add_argument("--detail", action="store_true", help="逐行列出命中")
    parser.add_argument("--only", choices=[LITERAL, COMMENT, DOC, OTHER], help="只列某一类")
    parser.add_argument("--fix", action="store_true", help="就地替换文本内容（须配 --replace）")
    options = parser.parse_args()

    term = options.term
    pattern = re.compile(re.escape(term), re.IGNORECASE)

    hits = {}                 # 相对路径 -> [(行号, 分类)...]
    filename_hits = []
    excluded_files = 0
    excluded_hits = 0

    for path in sorted(REPO.rglob("*")):
        if not path.is_file():
            continue
        relative_path = path.relative_to(REPO)
        name_hit = term in path.name.lower()

        if is_excluded(relative_path):
            text = read_text(path)
            if text is not None:
                count = len(pattern.findall(text))
                if count:
                    excluded_files += 1
                    excluded_hits += count
            if name_hit:
                filename_hits.append(("(已排除)", relative_path))
            continue

        text = read_text(path)
        if text is None:
            if name_hit:
                filename_hits.append(("(二进制)", relative_path))
            continue

        found = []
        for number, line in enumerate(text.splitlines(), 1):
            for match in pattern.finditer(line):
                found.append((number, classify(path, line, match.start())))
        if found:
            hits[relative_path] = found
        if name_hit:
            filename_hits.append(("", relative_path))

    total = sum(len(items) for items in hits.values())
    by_class = {}
    for items in hits.values():
        for _, kind in items:
            by_class[kind] = by_class.get(kind, 0) + 1

    print(f"扫描词: {term}（忽略大小写）")
    print(f"根目录: {REPO}")
    print()
    print(f"自研部分命中: {total} 次 / {len(hits)} 个文件")
    print()
    print("按分类:")
    for kind in (LITERAL, COMMENT, DOC, OTHER):
        if kind in by_class:
            print(f"  {kind:<6} {by_class[kind]:>5} 次")

    by_region = {}
    for relative_path, items in hits.items():
        region = region_of(relative_path)
        by_region[region] = by_region.get(region, 0) + len(items)
    print()
    print("按区域:")
    for region, count in sorted(by_region.items(), key=lambda item: -item[1]):
        print(f"  {region:<12} {count:>5} 次")

    print()
    print("命中文件（次数倒序，[L]字面量 [C]注释 [D]文档）:")
    for relative_path, items in sorted(hits.items(), key=lambda item: (-len(item[1]), item[0].as_posix())):
        if options.only:
            items = [item for item in items if item[1] == options.only]
            if not items:
                continue
        kinds = "".join(sorted({SHORT[kind] for _, kind in items}))
        print(f"  {len(items):>4} [{kinds}]  {relative_path.as_posix()}")

    if filename_hits:
        print()
        print(f"文件名本身含 {term}: {len(filename_hits)} 个（需 mv，未自动改）")
        for note, relative_path in filename_hits:
            print(f"  {relative_path.as_posix()}{' ' + note if note else ''}")

    print()
    print(f"已排除（第三方/构建产物/二进制）: {excluded_hits} 次 / {excluded_files} 个文件")

    if options.detail:
        print()
        print("逐行明细:")
        for relative_path, items in sorted(hits.items()):
            content = (REPO / relative_path).read_text(encoding="utf-8", errors="replace").splitlines()
            shown = False
            for number, kind in items:
                if options.only and kind != options.only:
                    continue
                if not shown:
                    print(f"--- {relative_path.as_posix()}")
                    shown = True
                print(f"  {number:>5} [{kind}] {content[number - 1].strip()[:140]}")

    if options.fix:
        if not options.replace:
            parser.error("--fix 需要同时给 --replace")
        changed_files = 0
        changed_hits = 0
        for relative_path, items in hits.items():
            full_path = REPO / relative_path
            content = full_path.read_text(encoding="utf-8", errors="replace")
            updated = replace_term(content, term, options.replace)
            if updated != content:
                full_path.write_text(updated, encoding="utf-8")
                changed_files += 1
                changed_hits += len(items)
        print()
        print(f"已替换: {changed_hits} 次 / {changed_files} 个文件 → {options.replace}")
        print("文件名未改，按上面清单自行 mv。")

    return 0


if __name__ == "__main__":
    sys.exit(main())
