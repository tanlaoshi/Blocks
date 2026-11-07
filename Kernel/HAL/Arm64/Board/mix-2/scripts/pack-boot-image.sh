#!/usr/bin/env bash
# pack-boot-image.sh — Build/MIX2 Kernel.bin → boot-chiron.img（fastboot boot）
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../../../../.." && pwd)"
KERNEL="${ROOT}/Kernel"
BUILDDIR="${BUILDDIR:-${KERNEL}/Build/MIX2}"
BIN="${BUILDDIR}/HAL/Arm64/Kernel.bin"
OUT="${BUILDDIR}/HAL/Arm64/boot-chiron.img"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

if [[ ! -f "$BIN" ]]; then
  echo "error: missing $BIN" >&2
  echo "  first: cd Kernel && ./build.sh arm64 BOARD=mix-2 BRINGUP=1 && make ARCH=arm64 BOARD=mix-2 BRINGUP=1 BUILDDIR=Build/MIX2 kernel-bin" >&2
  exit 1
fi

python3 "${SCRIPT_DIR}/pack-boot-image.py" "$BIN" -o "$OUT"
echo "fastboot boot: fastboot boot $OUT"
ls -la "$OUT"
