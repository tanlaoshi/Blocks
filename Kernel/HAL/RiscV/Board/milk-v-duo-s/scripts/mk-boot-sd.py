#!/usr/bin/env python3
"""Build a FIT boot.sd that stock Duo S sdboot can bootm.

Vendor bootcmd is `run sdboot` → fatload boot.sd → bootm #config-<FDT_NO>.
There is no distro boot_targets / boot.scr scan. ENV is often nowhere, so
saveenv cannot persist a new bootcmd. Replace boot.sd with this FIT (keep the
vendor file as boot.sd.vendor on the same FAT).

No dtc/mkimage required. Packs Kernel.elf PT_LOAD into a contiguous blob at
0x80200000 (same entry bootelf uses) and registers several Duo S config names
so a mismatched FDT_NO still matches.
"""
from __future__ import print_function

import argparse
import struct
import zlib
from pathlib import Path

# bootm ${uImage_addr}#config-<FDT_NO>; FDT_NO = CVICHIP_CVIBOARD (SDK).
# Include common Duo S / image-name variants; sdboot picks one.
CONFIG_NAMES = (
    "config-sg2000_milkv_duos_musl_riscv64_sd",
    "config-cv181x_milkv_duos_sd",
    "config-cv1813h_milkv_duos_sd",
    "config-sg2000_milkv_duos_sd",
    "config-cv181x_milkv_duos_musl_riscv64_sd",
    "config-cv1813h_milkv_duos_musl_riscv64_sd",
)

FDT_MAGIC = 0xD00DFEED
FDT_BEGIN_NODE = 1
FDT_END_NODE = 2
FDT_PROP = 3
FDT_END = 9

KERNEL_LOAD = 0x80200000


def elf64_le_loads(data: bytes):
    if data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
        raise SystemExit("need ELF64 little-endian Kernel.elf")
    e_entry = struct.unpack_from("<Q", data, 24)[0]
    e_phoff = struct.unpack_from("<Q", data, 32)[0]
    e_phentsize, e_phnum = struct.unpack_from("<HH", data, 54)
    loads = []
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        p_type = struct.unpack_from("<I", data, off)[0]
        if p_type != 1:  # PT_LOAD
            continue
        p_offset, p_vaddr, _p_paddr, p_filesz, p_memsz, _al = struct.unpack_from(
            "<QQQQQQ", data, off + 8
        )
        loads.append((p_offset, p_vaddr, p_filesz, p_memsz))
    if not loads:
        raise SystemExit("no PT_LOAD in ELF")
    return e_entry, loads


def flat_from_elf(data: bytes) -> tuple:
    e_entry, loads = elf64_le_loads(data)
    loads.sort(key=lambda x: x[1])
    base = loads[0][1]
    if base != KERNEL_LOAD:
        raise SystemExit(
            "unexpected first PT_LOAD %#x (want %#x)" % (base, KERNEL_LOAD)
        )
    if e_entry != KERNEL_LOAD:
        raise SystemExit("unexpected entry %#x (want %#x)" % (e_entry, KERNEL_LOAD))
    end = max(v + f for _o, v, f, _m in loads)
    blob = bytearray(end - base)
    for p_offset, p_vaddr, p_filesz, _m in loads:
        rel = p_vaddr - base
        blob[rel : rel + p_filesz] = data[p_offset : p_offset + p_filesz]
    return bytes(blob), e_entry


class FitBuilder:
    def __init__(self):
        self.strings = bytearray(b"\0")
        self.struct = bytearray()
        self._soff = {"": 0}

    def _str(self, s: str) -> int:
        if s in self._soff:
            return self._soff[s]
        off = len(self.strings)
        self.strings += s.encode("ascii") + b"\0"
        self._soff[s] = off
        return off

    def _align4(self):
        while len(self.struct) & 3:
            self.struct += b"\0"

    def begin(self, name: str = ""):
        self.struct += struct.pack(">I", FDT_BEGIN_NODE)
        raw = name.encode("ascii") + b"\0"
        self.struct += raw
        self._align4()

    def end(self):
        self.struct += struct.pack(">I", FDT_END_NODE)

    def prop(self, name: str, value: bytes):
        self.struct += struct.pack(">III", FDT_PROP, len(value), self._str(name))
        self.struct += value
        self._align4()

    def prop_str(self, name: str, value: str):
        self.prop(name, value.encode("ascii") + b"\0")

    def prop_u32(self, name: str, value: int):
        self.prop(name, struct.pack(">I", value & 0xFFFFFFFF))

    def finish(self) -> bytes:
        self.struct += struct.pack(">I", FDT_END)
        # reserve map: empty terminator
        rsv = b"\0" * 16
        off_dt_struct = 40 + len(rsv)
        off_dt_strings = off_dt_struct + len(self.struct)
        totalsize = off_dt_strings + len(self.strings)
        header = struct.pack(
            ">IIIIIIIIII",
            FDT_MAGIC,
            totalsize,
            off_dt_struct,
            off_dt_strings,
            40,  # off_mem_rsvmap
            17,  # version
            16,  # last_comp_version
            0,  # boot_cpuid_phys
            len(self.strings),
            len(self.struct),
        )
        assert len(header) == 40
        blob = header + rsv + self.struct + self.strings
        # header CRC not used by FDT; FIT optional hashes omitted (unsigned).
        return blob


def build_fit(kernel: bytes, entry: int, fdt: bytes) -> bytes:
    if len(fdt) < 8 or struct.unpack_from(">I", fdt, 0)[0] != FDT_MAGIC:
        raise SystemExit("fdt is not a DTB (bad magic)")
    b = FitBuilder()
    b.begin("")
    b.prop_str("description", "Blocks Milk-V Duo S")
    b.prop_u32("#address-cells", 1)

    b.begin("images")
    b.begin("kernel-1")
    b.prop_str("description", "Blocks Kernel.bin")
    b.prop("data", kernel)
    b.prop_str("type", "kernel")
    b.prop_str("arch", "riscv")
    b.prop_str("os", "linux")
    b.prop_str("compression", "none")
    b.prop_u32("load", KERNEL_LOAD)
    b.prop_u32("entry", entry)
    crc = zlib.crc32(kernel) & 0xFFFFFFFF
    b.begin("hash-1")
    b.prop_str("algo", "crc32")
    b.prop("value", struct.pack(">I", crc))
    b.end()
    b.end()  # kernel-1

    b.begin("fdt-1")
    b.prop_str("description", "Duo S DTB")
    b.prop("data", fdt)
    b.prop_str("type", "flat_dt")
    b.prop_str("arch", "riscv")
    b.prop_str("compression", "none")
    crc = zlib.crc32(fdt) & 0xFFFFFFFF
    b.begin("hash-1")
    b.prop_str("algo", "crc32")
    b.prop("value", struct.pack(">I", crc))
    b.end()
    b.end()  # fdt-1
    b.end()  # images

    b.begin("configurations")
    b.prop_str("default", CONFIG_NAMES[0])
    for name in CONFIG_NAMES:
        b.begin(name)
        b.prop_str("description", "Blocks")
        b.prop_str("kernel", "kernel-1")
        b.prop_str("fdt", "fdt-1")
        b.end()
    b.end()

    b.end()  # root
    return b.finish()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    here = Path(__file__).resolve().parent
    repo = here.parents[5]  # .../Blocks
    default_elf = repo / "Kernel" / "Build" / "HAL" / "RiscV" / "Kernel.elf"
    ap.add_argument(
        "-k",
        "--kernel",
        type=Path,
        default=default_elf,
        help="Kernel.elf (default: build tree RiscV)",
    )
    ap.add_argument(
        "-o",
        "--output",
        type=Path,
        default=here / "boot.sd",
        help="FIT output path (default: scripts/boot.sd)",
    )
    ap.add_argument(
        "-d",
        "--dtb",
        type=Path,
        default=here / "duo.dtb",
        help="DTB for FIT fdt-1 (default: scripts/duo.dtb)",
    )
    args = ap.parse_args()
    if not args.kernel.is_file():
        raise SystemExit("missing %s — build: ./build.sh riscv BOARD=milk-v-duo-s" % args.kernel)
    if not args.dtb.is_file():
        raise SystemExit(
            "missing %s — vendor DTB (card has none loose; keep scripts/duo.dtb)"
            % args.dtb
        )
    flat, entry = flat_from_elf(args.kernel.read_bytes())
    fdt = args.dtb.read_bytes()
    fit = build_fit(flat, entry, fdt)
    args.output.write_bytes(fit)
    print(
        "wrote %s (%d bytes FIT, %d bytes kernel @%#x, %d bytes dtb)"
        % (args.output, len(fit), len(flat), entry, len(fdt))
    )
    print("configs: %s" % ", ".join(CONFIG_NAMES))
    print(
        "card: mv boot.sd boot.sd.vendor && cp %s <LABEL=boot>/boot.sd && sync"
        % args.output.name
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
