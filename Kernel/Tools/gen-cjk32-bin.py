#!/usr/bin/env python3
"""生成 Runtime/RootFs/X64/CJK32.BIN（盘读字库；默认 32×32×4bpp）。

复用现网 Tools/Scripts/gen-cjk32.py 的 Noto 栅格逻辑；不写进 Kernel.elf。
用法：
  python3 Kernel/Tools/gen-cjk32-bin.py              # 32×32 GB2312
  python3 Kernel/Tools/gen-cjk32-bin.py --dim 24
"""
from __future__ import annotations

import argparse
import importlib.util
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "Runtime" / "RootFs" / "X64" / "CJK32.BIN"
GEN32 = (
    Path.home()
    / "tanlaoshi"
    / "edk2"
    / "ToyKernel"
    / "Tools"
    / "Scripts"
    / "gen-cjk32.py"
)


def load_gen32():
    spec = importlib.util.spec_from_file_location("gen_cjk32", GEN32)
    if spec is None or spec.loader is None:
        raise SystemExit(f"cannot load {GEN32}")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def main() -> int:
    ap = argparse.ArgumentParser(description="Build CJK32.BIN for Blocks Runtime")
    ap.add_argument(
        "--dim",
        type=int,
        default=18,
        choices=(16, 18, 20, 24, 26, 28, 32),
        help="glyph edge; 默认 18（与 Terminus 同行高）",
    )
    ap.add_argument("--bpp", type=int, default=4, choices=(4,))
    ap.add_argument("--set", choices=("gb2312", "ui"), default="gb2312")
    ap.add_argument("--out", type=Path, default=OUT)
    ap.add_argument("--size", type=int, default=0, help="Noto pt; 0=auto")
    args = ap.parse_args()

    g = load_gen32()
    dim = args.dim
    bpp = args.bpp
    size = args.size or {
        16: 14, 18: 15, 20: 17, 24: 20, 26: 22, 28: 24, 32: 27
    }[dim]

    loc = g.zh_locale_codepoints()
    src = g.source_string_codepoints()
    vocab: set[int] = set()
    g.add_text_cps(vocab, g.COMPUTER_VOCAB)
    if args.set == "gb2312":
        base = g.gb2312_codepoints()
        union = base | loc | src | vocab
    else:
        union = loc | src | vocab
    cps = sorted(union)

    font = g.load_font(size)
    bpr = (dim + 1) // 2
    bpg = dim * bpr
    bits: list[bytes] = []
    n = len(cps)
    for i, cp in enumerate(cps):
        rows = g.glyph_bits_4bpp(font, cp, dim)
        if len(rows) != bpg:
            print(f"bad glyph len {len(rows)} want {bpg}", file=sys.stderr)
            return 1
        bits.append(bytes(rows))
        if (i + 1) % 500 == 0 or i + 1 == n:
            print(f"  raster {i + 1}/{n}", file=sys.stderr)

    hdr = bytearray(32)
    hdr[0:4] = b"CJ32"
    struct.pack_into("<IIII", hdr, 4, len(cps), dim, bpp, bpg)
    out = bytearray(hdr)
    for c in cps:
        out += struct.pack("<I", c)
    for b in bits:
        out += b

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(out)
    print(
        f"wrote {args.out} ({len(out)} bytes, {len(cps)} glyphs, "
        f"{dim}x{dim}x{bpp}bpp, font_size={size})",
        file=sys.stderr,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
