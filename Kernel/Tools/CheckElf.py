#!/usr/bin/env python3
"""对照 Boot 的 ELF 头和内核 Elf.h。Boot 不包含内核头。"""
import re
import sys
from pathlib import Path

KERNEL = Path(__file__).resolve().parents[1]
REPO = KERNEL.parent
BOOT = REPO / "Boot/BootPkg/LoadKernel/Elf.c"
ELF_H = KERNEL / "Include/Core/Elf.h"

WIDTH = {"UINT8": 1, "UINT16": 2, "UINT32": 4, "UINT64": 8, "INT64": 8}
FIELD = re.compile(
    r"\b(UINT8|UINT16|UINT32|UINT64|INT64)\s+"
    r"([A-Za-z_][A-Za-z0-9_]*)(?:\s*\[\s*(\d+)\s*\])?"
)
STRUCT = re.compile(r"typedef\s+struct\s*\{(.*?)\}\s*(\w+)\s*;", re.S)


def die(message):
    raise SystemExit(f"error: {message}")


def define_int(text, name):
    match = re.search(rf"#define\s+{name}\s+(0x[0-9A-Fa-f]+|[0-9]+)U*", text)
    if match is None:
        die(f"{name} not found")
    return int(match.group(1), 0)


def struct_body(text, name):
    for body, found in STRUCT.findall(text):
        if found == name:
            return body
    die(f"struct {name} not found")


def fields(body, packed):
    found = []
    offset = 0
    align_max = 1
    for part in body.split(";"):
        part = re.sub(r"/\*.*?\*/", "", part, flags=re.S)
        match = FIELD.search(part)
        if match is None:
            continue
        kind, name, count = match.group(1), match.group(2), match.group(3)
        width = WIDTH[kind]
        size = width * (int(count) if count else 1)
        align = 1 if packed else width
        if offset % align:
            offset += align - (offset % align)
        found.append((name, offset, size))
        offset += size
        align_max = max(align_max, align)
    if not packed and offset % align_max:
        offset += align_max - (offset % align_max)
    return found, offset


def same_layout(boot_fields, kern_fields, label):
    boot_index = 0
    kern_index = 0
    while boot_index < len(boot_fields) or kern_index < len(kern_fields):
        if boot_index >= len(boot_fields) or kern_index >= len(kern_fields):
            die(f"{label} field count differs")
        boot_name, boot_at, boot_size = boot_fields[boot_index]
        kern_name, kern_at, kern_size = kern_fields[kern_index]
        if boot_at != kern_at:
            die(f"{label} {boot_name} at {boot_at}, {kern_name} at {kern_at}")
        if boot_size == kern_size:
            boot_index += 1
            kern_index += 1
            continue
        if kern_size < boot_size:
            die(f"{label} {boot_name} is wider than {kern_name}")
        covered = 0
        while boot_index < len(boot_fields) and covered < kern_size:
            name, at, size = boot_fields[boot_index]
            if at != boot_at + covered:
                die(f"{label} {name} breaks {kern_name}")
            covered += size
            boot_index += 1
        if covered != kern_size:
            die(f"{label} {kern_name} size {kern_size} covered {covered}")
        kern_index += 1


def main():
    boot_text = BOOT.read_text()
    elf_text = ELF_H.read_text()
    if define_int(elf_text, "ELF_MAGIC") != 0x464C457F:
        die("ELF_MAGIC is not 0x464C457F")
    if "0x464C457F" not in boot_text:
        die("Boot magic is not 0x464C457F")
    if define_int(elf_text, "PT_LOAD") != 1 or define_int(boot_text, "PT_LOAD") != 1:
        die("PT_LOAD is not 1")
    if define_int(elf_text, "ELFCLASS64") != 2:
        die("ELFCLASS64 is not 2")
    if define_int(elf_text, "ELFDATA2LSB") != 1:
        die("ELFDATA2LSB is not 1")
    if "ElfBase + 4) != 2" not in boot_text:
        die("Boot does not require ELF class 2")

    boot_ehdr, boot_ehdr_size = fields(struct_body(boot_text, "ELF_HEADER_64"), True)
    kern_ehdr, kern_ehdr_size = fields(struct_body(elf_text, "Elf64_Ehdr"), False)
    boot_phdr, boot_phdr_size = fields(struct_body(boot_text, "PROGRAM_HEADER_64"), True)
    kern_phdr, kern_phdr_size = fields(struct_body(elf_text, "Elf64_Phdr"), False)
    if boot_ehdr_size != kern_ehdr_size:
        die(f"Ehdr size Boot {boot_ehdr_size} kernel {kern_ehdr_size}")
    if boot_phdr_size != kern_phdr_size:
        die(f"Phdr size Boot {boot_phdr_size} kernel {kern_phdr_size}")
    same_layout(boot_ehdr, kern_ehdr, "Ehdr")
    same_layout(boot_phdr, kern_phdr, "Phdr")
    endian = next(item for item in boot_ehdr if item[0] == "Endianness")
    if endian[1] != 5 or endian[2] != 1:
        die("Endianness is not the ELFDATA2LSB byte")
    print(f"ELF headers OK (ehdr {boot_ehdr_size}, phdr {boot_phdr_size})")


if __name__ == "__main__":
    sys.exit(main() or 0)
