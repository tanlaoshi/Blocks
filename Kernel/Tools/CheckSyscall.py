#!/usr/bin/env python3
"""核对 SYS_*：头文件的号、SyscallDispatch 的 case、用户态汇编里的号。"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ABI = ROOT / "Include/Abi/SyscallABI.h"
DISPATCH = ROOT / "Core/Syscall/Syscall.c"
DEFINE = re.compile(r"^\s*#define\s+(SYS_[A-Z0-9_]+)\s+(\d+)\b", re.M)
CASE = re.compile(r"\bcase\s+(SYS_[A-Z0-9_]+)\s*:")
TOKEN = re.compile(r"\b(SYS_[A-Z0-9_]+)\b")
NOTE = re.compile(r"\b(SYS_[A-Z0-9_]+)\s*=\s*(\d+)\b")


def defines(text):
    found = DEFINE.findall(text)
    names = {}
    for name, num in found:
        num = int(num)
        if name in names and names[name] != num:
            raise SystemExit(f"error: {name} defined twice")
        names[name] = num
    by_num = {}
    for name, num in names.items():
        if num in by_num:
            raise SystemExit(f"error: {name} and {by_num[num]} both use {num}")
        by_num[num] = name
    return names


def dispatch_names(text):
    start = text.find("UINT64 SyscallDispatch")
    if start < 0:
        raise SystemExit("error: SyscallDispatch not found")
    body = text[start:]
    end = body.find("\n}")
    if end < 0:
        raise SystemExit("error: SyscallDispatch has no closing brace")
    return set(CASE.findall(body[: end + 2]))


def user_asm():
    files = sorted((ROOT / "User").rglob("*.S"))
    files = [p for p in files if p.name.startswith("syscall") or p.name.startswith("crt0") or "Apps" in p.parts]
    return files


def main():
    abi = defines(ABI.read_text(encoding="utf-8"))
    cases = dispatch_names(DISPATCH.read_text(encoding="utf-8"))
    missing = sorted(set(abi) - cases)
    extra = sorted(cases - set(abi))
    if missing or extra:
        for name in missing:
            print(f"error: {name} is {abi[name]} in SyscallABI.h but has no case", file=sys.stderr)
        for name in extra:
            print(f"error: case {name} is not a SYS_* number", file=sys.stderr)
        raise SystemExit(1)
    for path in user_asm():
        text = path.read_text(encoding="utf-8")
        for name in TOKEN.findall(text):
            if name not in abi:
                print(f"error: {path.relative_to(ROOT)} uses {name}, not in SyscallABI.h", file=sys.stderr)
                raise SystemExit(1)
        for name, num in NOTE.findall(text):
            num = int(num)
            if name not in abi or abi[name] != num:
                got = abi.get(name, "missing")
                print(f"error: {path.relative_to(ROOT)} says {name}={num}, header has {got}", file=sys.stderr)
                raise SystemExit(1)
    print(f"Syscall numbers OK ({len(abi)})")


if __name__ == "__main__":
    main()
