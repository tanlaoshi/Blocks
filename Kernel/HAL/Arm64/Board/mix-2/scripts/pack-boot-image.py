#!/usr/bin/env python3
"""
pack-boot-image.py — Kernel.bin → gzip[+dtb] → Android boot.img v0（chiron）

对表线刷包：gzip(Image) 后追加 DTB；kernel_addr=0x8000 → phys 0x80008000。
DTB 默认取自线刷 boot 解出的首个 FDT（可用 --dtb 覆写）。
仅供 fastboot boot，不 flash。
"""
from __future__ import annotations

import argparse
import gzip
import io
import os
import struct
import sys

BOOT_MAGIC = b"ANDROID!"
PAGE_SIZE = 4096
KERNEL_ADDR = 0x00008000
RAMDISK_ADDR = 0x01000000
TAGS_ADDR = 0x00000100

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
DEFAULT_DTB = os.path.join(SCRIPT_DIR, "chiron-stock.dtb")


def align_up(Value: int, Align: int) -> int:
    return (Value + Align - 1) & ~(Align - 1)


def gzip_kernel(Plain: bytes) -> bytes:
    Buffer = io.BytesIO()
    with gzip.GzipFile(fileobj=Buffer, mode="wb", compresslevel=9, mtime=0) as Gz:
        Gz.write(Plain)
    return Buffer.getvalue()


def pack(KernelPath: str, OutPath: str, DtbPath: str | None) -> None:
    with open(KernelPath, "rb") as File:
        Plain = File.read()
    if len(Plain) < 64:
        raise SystemExit(f"error: kernel too small: {KernelPath}")
    Magic = Plain[56:60]
    if Magic != b"ARM\x64":
        print(f"warning: Image magic {Magic!r} ≠ ARM\\x64 — 仍打包", file=sys.stderr)

    Blob = gzip_kernel(Plain)
    DtbLen = 0
    if DtbPath and os.path.isfile(DtbPath):
        with open(DtbPath, "rb") as DtbFile:
            Dtb = DtbFile.read()
        if Dtb[:4] != b"\xd0\x0d\xfe\xed":
            raise SystemExit(f"error: not FDT: {DtbPath}")
        Blob += Dtb
        DtbLen = len(Dtb)
    elif DtbPath:
        print(f"warning: missing DTB {DtbPath} — 仅 gzip", file=sys.stderr)

    KernelSize = len(Blob)

    Header = bytearray()
    Header += BOOT_MAGIC
    Header += struct.pack(
        "<IIIIIIII",
        KernelSize,
        KERNEL_ADDR,
        0,
        RAMDISK_ADDR,
        0,
        0,
        TAGS_ADDR,
        PAGE_SIZE,
    )
    Header += struct.pack("<I", 0)
    Header += b"blocks\x00".ljust(16, b"\x00")
    Header += b"\x00" * 512
    Header += b"\x00" * (PAGE_SIZE - len(Header))
    if len(Header) != PAGE_SIZE:
        raise SystemExit(f"error: header page {len(Header)} ≠ {PAGE_SIZE}")

    KernelPad = align_up(KernelSize, PAGE_SIZE) - KernelSize
    Image = bytes(Header) + Blob + (b"\x00" * KernelPad)

    os.makedirs(os.path.dirname(OutPath) or ".", exist_ok=True)
    with open(OutPath, "wb") as Out:
        Out.write(Image)
    print(
        f"Wrote {OutPath} ({len(Image)} bytes; plain={len(Plain)}; "
        f"gzip+dtb={KernelSize} dtb={DtbLen}; kernel_addr=0x{KERNEL_ADDR:x}; "
        f"load≈0x{0x80000000 + KERNEL_ADDR:x})"
    )


def main() -> None:
    Parser = argparse.ArgumentParser(description=__doc__)
    Parser.add_argument("kernel_bin", help="objcopy -O binary 产物（含 Image 头）")
    Parser.add_argument("-o", "--output", required=True, help="boot-chiron.img 路径")
    Parser.add_argument(
        "--dtb",
        default=DEFAULT_DTB,
        help=f"追加的 FDT（默认 {DEFAULT_DTB}）",
    )
    Args = Parser.parse_args()
    pack(Args.kernel_bin, Args.output, Args.dtb)


if __name__ == "__main__":
    main()
