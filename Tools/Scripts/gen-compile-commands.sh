#!/bin/bash
# gen-compile-commands.sh — 为 clangd 生成 Kernel/compile_commands.json（无需 bear/sudo）
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
export PATH="${HOME}/.local/bin:${PATH}"
python3 - "$ROOT" <<'PY'
import json, sys
from pathlib import Path

blocks = Path(sys.argv[1])
root = blocks / "Kernel"
hal = "X64"
dirs = [
    root / "BringUp",
    root / "Core",
    root / "Services",
    root / "Library",
    root / "HAL" / "Slots",
    root / "HAL" / hal,
    root / "HAL" / "Virtio",
]
skip_parts = {"ThirdParty", "Build", ".git"}
incs = [
    f"-I{root}/Include",
    f"-I{root}/Include/Abi",
    f"-I{root}/Include/Hal",
    f"-I{root}/Include/Core",
    f"-I{root}/Include/Driver",
    f"-I{root}/Include/Library",
    f"-I{root}/Include/Services",
    f"-I{root}/Library",
    f"-I{root}/Library/Fonts",
    f"-I{root}/ThirdParty/stb",
    f"-I{root}/ThirdParty/freetype-port",
    f"-I{root}/ThirdParty/freetype/include",
    f"-I{root}/HAL/{hal}",
    f"-I{root}/HAL/{hal}/Hal",
    f"-I{root}/HAL/{hal}/Architecture",
    f"-I{root}/HAL/{hal}/PageTable",
    f"-I{root}/HAL/{hal}/PCIe",
    f"-I{root}/HAL/{hal}/IoApic",
    f"-I{root}/HAL/{hal}/DeviceEnum",
    f"-I{root}/HAL/{hal}/SmpBoot",
    f"-I{root}/HAL/{hal}/Platform",
    f"-I{root}/HAL/{hal}/Acpi",
    f"-I{root}/BringUp/{hal}",
    f"-I{root}/HAL/{hal}/Drivers",
    f"-I{root}/HAL/{hal}/Drivers/Bus",
    f"-I{root}/HAL/{hal}/Drivers/Input",
    f"-I{root}/HAL/{hal}/Drivers/Block",
    f"-I{root}/HAL/{hal}/Drivers/Video/Igpu",
    f"-I{root}/HAL/{hal}/Drivers/Model",
    f"-I{root}/HAL",
    f"-I{root}/HAL/{hal}/LwIp/include",
    f"-I{root}/HAL/{hal}/LwIp",
    f"-I{root}/ThirdParty/lwip/src/include",
    f"-I{root}/User/include",
]
flags = [
    "clang", "-std=c99", "-ffreestanding", "-nostdlib",
    "-fno-builtin", "-fno-pie", "-fno-pic", "-fno-stack-protector",
    "-m64", "-mno-red-zone",
    "-DKERNEL_DEBUG=0", "-DLWIP_ENABLE=1", "-DDEMO_DRIVER=1",
] + incs
entries = []
for base in dirs:
    if not base.is_dir():
        continue
    for p in base.rglob("*.c"):
        if any(s in p.parts for s in skip_parts) or "_template" in p.parts:
            continue
        entries.append({
            "directory": str(root),
            "file": str(p),
            "arguments": flags + ["-c", str(p)],
        })
user = root / "User"
if user.is_dir():
    for p in user.rglob("*.c"):
        if "Package/build" in str(p) or "Build" in p.parts:
            continue
        uflags = [
            "clang", "-std=c99", "-ffreestanding", "-nostdlib",
            "-fno-builtin", "-fno-pie", "-fno-pic",
            f"-I{root}/User/include",
            f"-I{root}/Include",
            f"-I{root}/Include/Abi",
        ]
        entries.append({
            "directory": str(root),
            "file": str(p),
            "arguments": uflags + ["-c", str(p)],
        })
out = root / "compile_commands.json"
out.write_text(json.dumps(entries, indent=2) + "\n")
link = blocks / "compile_commands.json"
if link.exists() or link.is_symlink():
    link.unlink()
link.symlink_to("Kernel/compile_commands.json")
print(f"OK {out} ({len(entries)} entries)")
PY
