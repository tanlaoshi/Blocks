#!/usr/bin/env bash
# pack-boot-image.sh — Build/PIXEL8 Kernel.bin → boot.img + AVB hash footer（fastboot boot）
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../../../../.." && pwd)"
KERNEL="${ROOT}/Kernel"
BUILDDIR="${BUILDDIR:-${KERNEL}/Build/PIXEL8}"
BIN="${BUILDDIR}/HAL/Arm64/Kernel.bin"
OUT="${BUILDDIR}/HAL/Arm64/boot.img"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
AVBTOOL="${AVBTOOL:-${SCRIPT_DIR}/avbtool.py}"

if [[ ! -f "$BIN" ]]; then
  echo "error: missing $BIN" >&2
  echo "  first: cd Kernel && ./build.sh arm64 BOARD=pixel-8 BRINGUP=1 && make ARCH=arm64 BOARD=pixel-8 BRINGUP=1 BUILDDIR=Build/PIXEL8 kernel-bin" >&2
  exit 1
fi

python3 "${SCRIPT_DIR}/pack-boot-image.py" "$BIN" -o "$OUT"

if [[ ! -f "$AVBTOOL" ]]; then
  echo "error: missing $AVBTOOL（ABL 无 AVB footer 会直接复位）" >&2
  exit 1
fi

# 动态分区大小：避免垫成整颗 boot_a（64MiB）仍带合法 AVBf
python3 "$AVBTOOL" add_hash_footer \
  --image "$OUT" \
  --dynamic_partition_size \
  --partition_name boot

echo "fastboot boot: fastboot boot $OUT"
ls -la "$OUT"
