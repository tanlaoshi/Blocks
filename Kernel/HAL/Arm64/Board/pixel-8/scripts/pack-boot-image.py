#!/usr/bin/env python3
"""
pack-boot-image.py — Kernel.bin（含 Linux Image 头）→ Android boot.img v4

对表 AOSP system/tools/mkbootimg：
  header_size 字段 = sizeof(boot_img_hdr_v4)=1584（勿写 4096）
  头区再 pad 到 4096 页；其后跟 kernel（按页对齐）。
仅供 Pixel 8 fastboot boot，不 flash。
"""
from __future__ import annotations

import argparse
import os
import struct
import sys

BOOT_MAGIC = b"ANDROID!"
HEADER_VERSION = 4
PAGE_SIZE = 4096
# boot_img_hdr_v4 结构体大小（AOSP BOOT_IMAGE_HEADER_V4_SIZE）
HEADER_STRUCT_SIZE = 1584
KERNEL_LOAD = 0x90701000


def align_up(Value: int, Align: int) -> int:
    return (Value + Align - 1) & ~(Align - 1)


def pack(KernelPath: str, OutPath: str) -> None:
    with open(KernelPath, "rb") as File:
        Kernel = File.read()
    if len(Kernel) < 64:
        raise SystemExit(f"error: kernel too small: {KernelPath}")
    Magic = Kernel[56:60]
    if Magic != b"ARM\x64":
        print(f"warning: Image magic {Magic!r} ≠ ARM\\x64 — 仍打包", file=sys.stderr)

    KernelSize = len(Kernel)
    Cmdline = b"console=ttySAC0,115200n8 earlycon=exynos4210,0x10870000\x00"
    Cmdline = Cmdline.ljust(1536, b"\x00")

    Header = bytearray()
    Header += BOOT_MAGIC
    Header += struct.pack("<I", KernelSize)
    Header += struct.pack("<I", 0)  # ramdisk_size
    Header += struct.pack("<I", 0)  # os_version | patch
    Header += struct.pack("<I", HEADER_STRUCT_SIZE)
    Header += struct.pack("<4I", 0, 0, 0, 0)
    Header += struct.pack("<I", HEADER_VERSION)
    Header += Cmdline
    Header += struct.pack("<I", 0)  # signature_size（unlocked fastboot boot）
    if len(Header) != HEADER_STRUCT_SIZE:
        raise SystemExit(f"error: header struct {len(Header)} ≠ {HEADER_STRUCT_SIZE}")
    Header += b"\x00" * (PAGE_SIZE - len(Header))

    KernelPad = align_up(KernelSize, PAGE_SIZE) - KernelSize
    Image = bytes(Header) + Kernel + (b"\x00" * KernelPad)

    os.makedirs(os.path.dirname(OutPath) or ".", exist_ok=True)
    with open(OutPath, "wb") as Out:
        Out.write(Image)
    print(
        f"Wrote {OutPath} ({len(Image)} bytes; kernel={KernelSize}; "
        f"hdr_struct={HEADER_STRUCT_SIZE}; load=0x{KERNEL_LOAD:x})"
    )


def main() -> None:
    Parser = argparse.ArgumentParser(description=__doc__)
    Parser.add_argument("kernel_bin", help="objcopy -O binary 产物（含 Image 头）")
    Parser.add_argument("-o", "--output", required=True, help="boot.img 路径")
    Args = Parser.parse_args()
    pack(Args.kernel_bin, Args.output)


if __name__ == "__main__":
    main()
