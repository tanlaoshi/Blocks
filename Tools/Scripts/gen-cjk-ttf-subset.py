#!/usr/bin/env python3
"""font-unified-ttf：同源子集（拉丁+汉字，TrueType glyf）。

默认源：AR PL UMing CN（uming.ttc face0）— 同文件含 ASCII 与汉字。
禁 Noto CJK TTC（CFF，stb 栅不出字）。Droid Fallback 几乎无拉丁，不适合同源。
"""
from __future__ import annotations

import argparse
import sys
import tempfile
from pathlib import Path

from fontTools.subset import Options, Subsetter
from fontTools.ttLib import TTCollection, TTFont

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_TTC = Path("/usr/share/fonts/truetype/arphic/uming.ttc")
DEFAULT_SEED = ROOT / "Runtime" / "RootFs" / "X64" / "Assets" / "Fonts" / "CJK.TTF"
DEFAULT_OUT = ROOT / "Tools" / "Fonts" / "CJK.TTF"


def load_src(path: Path, face: int) -> TTFont:
    if path.suffix.lower() == ".ttc":
        Col = TTCollection(str(path))
        if face < 0 or face >= len(Col.fonts):
            raise SystemExit(f"face {face} out of range 0..{len(Col.fonts)-1}")
        # subsetter 要文件路径或独立 TTFont；先落临时单面
        Tmp = Path(tempfile.mkstemp(suffix=".ttf")[1])
        Col.fonts[face].save(Tmp)
        return TTFont(Tmp), Tmp
    return TTFont(path), None


def main() -> int:
    Ap = argparse.ArgumentParser()
    Ap.add_argument("--src", type=Path, default=DEFAULT_TTC)
    Ap.add_argument("--face", type=int, default=0, help="TTC 面（UMing CN=0）")
    Ap.add_argument("--seed", type=Path, default=DEFAULT_SEED)
    Ap.add_argument("--out", type=Path, default=DEFAULT_OUT)
    Args = Ap.parse_args()

    if not Args.src.is_file():
        print(f"missing src {Args.src}", file=sys.stderr)
        return 1

    Unicodes: set[int] = set(range(0x20, 0x7F))
    Unicodes.update(
        [0xA0, 0xB7, 0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2026,
         0x3001, 0x3002]
    )
    if Args.seed.is_file():
        SeedMap = TTFont(Args.seed).getBestCmap() or {}
        Unicodes.update(SeedMap.keys())

    Font, Tmp = load_src(Args.src, Args.face)
    try:
        Opt = Options()
        Opt.notdef_outline = True
        Opt.recommended_glyphs = True
        Opt.layout_closure = False
        Sub = Subsetter(options=Opt)
        Sub.populate(unicodes=Unicodes)
        Sub.subset(Font)
        if "glyf" not in Font:
            print("subset has no glyf", file=sys.stderr)
            return 1
        Args.out.parent.mkdir(parents=True, exist_ok=True)
        Font.save(Args.out)
    finally:
        if Tmp is not None:
            Tmp.unlink(missing_ok=True)

    Cmap = TTFont(Args.out).getBestCmap() or {}
    print(
        f"wrote {Args.out} bytes={Args.out.stat().st_size} "
        f"glyphs={len(Cmap)} A={ord('A') in Cmap} CJK={0x4E2D in Cmap}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
