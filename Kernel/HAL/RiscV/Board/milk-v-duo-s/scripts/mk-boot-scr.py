#!/usr/bin/env python3
"""Wrap boot.cmd as U-Boot legacy IH_TYPE_SCRIPT image (boot.scr).

No u-boot-tools required. Output next to this script unless -o given.
"""
from __future__ import print_function

import argparse
import struct
import time
import zlib
from pathlib import Path

IH_MAGIC = 0x27051956
IH_TYPE_SCRIPT = 6


def wrap_script(text: bytes) -> bytes:
    if not text.endswith(b"\n"):
        text += b"\n"
    payload = struct.pack(">II", len(text), 0) + text
    t = int(time.time()) & 0xFFFFFFFF
    size = len(payload)
    dcrc = zlib.crc32(payload) & 0xFFFFFFFF
    name = b"blocks-duo".ljust(32, b"\0")[:32]
    tmp = struct.pack(">IIIIIII", IH_MAGIC, 0, t, size, 0, 0, dcrc)
    tmp += bytes([0, 0, IH_TYPE_SCRIPT, 0]) + name
    hcrc = zlib.crc32(tmp) & 0xFFFFFFFF
    hdr = struct.pack(">IIIIIII", IH_MAGIC, hcrc, t, size, 0, 0, dcrc)
    hdr += bytes([0, 0, IH_TYPE_SCRIPT, 0]) + name
    assert len(hdr) == 64
    return hdr + payload


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    here = Path(__file__).resolve().parent
    ap.add_argument("-i", "--input", type=Path, default=here / "boot.cmd")
    ap.add_argument("-o", "--output", type=Path, default=here / "boot.scr")
    args = ap.parse_args()
    text = args.input.read_bytes()
    # Scripts must be ASCII for U-Boot parser.
    text.decode("ascii")
    out = wrap_script(text)
    args.output.write_bytes(out)
    print("wrote", args.output, "(%d bytes)" % len(out))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
