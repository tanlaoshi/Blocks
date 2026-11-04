#!/bin/bash
# build.sh — Duo BootStub ELF @0x83000000
#
# 产物：Build/DUO/BootStub/BootStub.elf（给 mk-boot-sd.py --stub）
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
BOARD_DIR="$(cd "$HERE/.." && pwd)"
BLOCKS_ROOT="$(cd "$BOARD_DIR/../../../../.." && pwd)"
KERNEL="$BLOCKS_ROOT/Kernel"
OUT="${BLOCKS_DUO_BUILD_DIR:-$KERNEL/Build/DUO}/BootStub"
OBJ="$OUT/obj"

TOOLS_ROOT="${BLOCKS_TOOLS_ROOT:-$BLOCKS_ROOT/Tools}"
if ls "$TOOLS_ROOT"/xpack-riscv-none-elf-gcc-*/bin/riscv-none-elf-gcc >/dev/null 2>&1; then
  CC="$(ls -d "$TOOLS_ROOT"/xpack-riscv-none-elf-gcc-*/bin/riscv-none-elf-gcc | tail -1)"
elif command -v riscv64-linux-gnu-gcc >/dev/null 2>&1; then
  CC=riscv64-linux-gnu-gcc
elif command -v riscv64-unknown-elf-gcc >/dev/null 2>&1; then
  CC=riscv64-unknown-elf-gcc
else
  echo "error: no riscv64 gcc" >&2
  exit 1
fi

CFLAGS=(
  -std=c99 -pedantic -ffreestanding -nostdlib -O2 -Wall -Wextra
  -fno-stack-protector -fno-builtin -fno-pic -fno-pie
  -march=rv64imac_zicsr_zifencei -mabi=lp64 -mcmodel=medany
  -I"$KERNEL/Include"
  -I"$KERNEL/Include/Abi"
  -I"$KERNEL/Include/Hal"
  -I"$KERNEL/Include/Core"
  -I"$KERNEL/Include/Driver"
  -I"$KERNEL/Include/Library"
  -I"$KERNEL/Library"
  -I"$KERNEL/HAL/RiscV"
  -I"$KERNEL/HAL/RiscV/Hal"
  -I"$KERNEL/HAL/Common"
  -I"$BOARD_DIR"
  -I"$BOARD_DIR/Sdhci"
  -DBOOT_STUB=1
)

mkdir -p "$OBJ"
compile() {
  local Src="$1"
  local Dst="$2"
  mkdir -p "$(dirname "$Dst")"
  echo "  CC $(basename "$Src")"
  "$CC" "${CFLAGS[@]}" -c "$Src" -o "$Dst"
}

STUB_SRCS=(
  "$HERE/Main.c"
  "$HERE/Serial.c"
  "$HERE/HostBootstrap.c"
  "$HERE/Volume.c"
  "$HERE/SelectKernelImage.c"
  "$HERE/ExecutableAndLinkableFormat.c"
)
SDHCI_SRCS=(
  "$BOARD_DIR/Sdhci/Card.c"
  "$BOARD_DIR/Sdhci/Command.c"
  "$BOARD_DIR/Sdhci/Transfer.c"
  "$BOARD_DIR/Sdhci/Utility.c"
)
LIB_SRCS=(
  "$KERNEL/Library/Block/Block.c"
  "$KERNEL/Library/LibWrite.c"
  "$KERNEL/Library/Text.c"
  "$KERNEL/Library/CString.c"
  "$KERNEL/Library/Gpt/Gpt.c"
  "$KERNEL/Library/Fat/Fat.c"
  "$KERNEL/Library/Fat/File.c"
  "$KERNEL/Library/Fat/FileRead.c"
  "$KERNEL/Library/Fat/FileWrite.c"
  "$KERNEL/Library/Fat/FileWriteExist.c"
  "$KERNEL/Library/Fat/FileWriteNew.c"
  "$KERNEL/Library/Fat/InputOutput.c"
  "$KERNEL/Library/Fat/Path.c"
  "$KERNEL/Library/Fat/PathName.c"
  "$KERNEL/Library/Fat/Directory.c"
  "$KERNEL/Library/Fat/DirectoryName.c"
  "$KERNEL/Library/Fat/DirectoryScan.c"
  "$KERNEL/Library/Fat/DirectorySlot.c"
)

OBJS=()
compile "$HERE/Entry.S" "$OBJ/Entry.o"
OBJS+=("$OBJ/Entry.o")

for Src in "${STUB_SRCS[@]}" "${SDHCI_SRCS[@]}" "${LIB_SRCS[@]}"; do
  Base="$(basename "$Src" | sed 's/\.[cS]$//')"
  Dst="$OBJ/$Base.o"
  compile "$Src" "$Dst"
  OBJS+=("$Dst")
done

echo "  LD BootStub.elf"
"$CC" -nostdlib -static -Wl,-T,"$HERE/link.ld" -o "$OUT/BootStub.elf" "${OBJS[@]}"
echo "wrote $OUT/BootStub.elf ($(stat -c '%s' "$OUT/BootStub.elf") bytes)"
