#!/bin/bash
# ck-duo-sd.sh — Duo 刷卡：Kernel 只在 LABEL=BLOCKS（对齐 X64/Pi）
#
#   source ~/Blocks/env.sh && ck-duo-sd
#   ck-duo-sd --blocks-only   # 只更新 BLOCKS/Kernel.elf（不碰 boot）
#   ck-duo-sd --wait=120
#   ck-duo-sd --no-eject
#
# 默认：BLOCKS 为唯一 Kernel.elf；boot 上删除 Kernel.elf；并刷新
# boot.sd（FIT 由 BLOCKS/Kernel.elf 打包，因厂商 sdboot 只能 bootm）、
# boot.scr（fatload mmc 0:4）、duo.dtb。不删 fip.bin。
#
# 禁止：对 Duo 跑 ck-pi-sd；格式化 boot；无备份删 boot.sd。
# 不 commit / 不 push。
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
BOARD_DIR="$(cd "$HERE/.." && pwd)"
# milk-v-duo-s → Board → RiscV → HAL → Kernel → Blocks
BLOCKS_ROOT="$(cd "$BOARD_DIR/../../../../.." && pwd)"
ELF="${BLOCKS_KERNEL_ELF:-$BLOCKS_ROOT/Kernel/Build/HAL/RiscV/Kernel.elf}"
WAIT_SEC="${CK_WAIT_SEC:-90}"
DO_EJECT=1
BLOCKS_ONLY=0

for A in "$@"; do
  case "$A" in
    --no-eject) DO_EJECT=0 ;;
    --blocks-only) BLOCKS_ONLY=1 ;;
    --boot) ;; # 兼容旧口令：默认已含 boot 刷新
    --wait=*) WAIT_SEC="${A#--wait=}" ;;
    -h|--help)
      sed -n '2,16p' "$0" | sed 's/^# \?//'
      exit 0
      ;;
    *)
      echo "error: unknown arg: $A" >&2
      exit 1
      ;;
  esac
done

find_mnt() {
  local Label=$1
  local d Dev
  for d in \
    /media/"${USER:-tank}"/"$Label" \
    /run/media/"${USER:-tank}"/"$Label" \
    /media/*/"$Label"; do
    if [ -d "$d" ] && [ -w "$d" ]; then
      printf '%s\n' "$d"
      return 0
    fi
  done
  Dev="$(lsblk -ln -o NAME,LABEL | awk -v L="$Label" '$2==L{print "/dev/"$1; exit}')"
  if [ -n "$Dev" ] && command -v udisksctl >/dev/null 2>&1; then
    udisksctl mount -b "$Dev" >/dev/null 2>&1 || true
    for d in /media/"${USER:-tank}"/"$Label" /run/media/"${USER:-tank}"/"$Label"; do
      if [ -d "$d" ] && [ -w "$d" ]; then
        printf '%s\n' "$d"
        return 0
      fi
    done
  fi
  return 1
}

if [ ! -f "$ELF" ]; then
  echo "error: missing $ELF（先: build riscv BOARD=milk-v-duo-s）" >&2
  exit 1
fi

echo "ck-duo-sd: ELF=$ELF  wait≤${WAIT_SEC}s  blocks_only=$BLOCKS_ONLY"

BlocksMnt=""
Elapsed=0
while [ "$Elapsed" -le "$WAIT_SEC" ]; do
  if BlocksMnt="$(find_mnt BLOCKS)"; then
    break
  fi
  if [ "$Elapsed" -eq 0 ]; then
    echo "ck-duo-sd: 等待 LABEL=BLOCKS…"
  fi
  sleep 2
  Elapsed=$((Elapsed + 2))
done
if [ -z "$BlocksMnt" ]; then
  echo "error: ${WAIT_SEC}s 内未检测到 BLOCKS（Duo 须另有 FAT32 LABEL=BLOCKS，勿 ck-pi-sd）" >&2
  exit 1
fi
echo "ck-duo-sd: BLOCKS=$BlocksMnt"

cp -f "$ELF" "$BlocksMnt/Kernel.elf"
cmp -s "$ELF" "$BlocksMnt/Kernel.elf" || {
  echo "error: BLOCKS Kernel.elf 校验失败" >&2
  exit 1
}
echo "  + BLOCKS Kernel.elf  (canonical)"

IdSrc="$BLOCKS_ROOT/Runtime/RootFs/X64/BLOCKS.ID"
if [ ! -f "$BlocksMnt/BLOCKS.ID" ]; then
  if [ -f "$IdSrc" ]; then
    cp -f "$IdSrc" "$BlocksMnt/BLOCKS.ID"
  else
    printf 'Blocks root volume\n' >"$BlocksMnt/BLOCKS.ID"
  fi
  echo "  + BLOCKS BLOCKS.ID"
else
  echo "  · BLOCKS.ID 已有"
fi

BootMnt="$(find_mnt boot)" || true
if [ -n "${BootMnt:-}" ] && [ -f "$BootMnt/Kernel.elf" ]; then
  rm -f "$BootMnt/Kernel.elf"
  echo "  - boot removed Kernel.elf"
fi

if [ "$BLOCKS_ONLY" = 0 ]; then
  if [ -z "${BootMnt:-}" ]; then
    echo "error: 需要 LABEL=boot（或改用 --blocks-only）" >&2
    exit 1
  fi
  if [ ! -f "$BootMnt/fip.bin" ]; then
    echo "error: boot 上无 fip.bin — 不像 Duo 厂商链，中止" >&2
    exit 1
  fi
  echo "ck-duo-sd: boot=$BootMnt"
  # FIT 必须内嵌核（sdboot=bootm）；真源仍是 BLOCKS/Kernel.elf
  python3 "$HERE/mk-boot-sd.py" -k "$BlocksMnt/Kernel.elf" -o "$HERE/boot.sd"
  python3 "$HERE/mk-boot-scr.py"
  if [ ! -f "$BootMnt/boot.sd.vendor" ] && [ -f "$BootMnt/boot.sd" ]; then
    mv "$BootMnt/boot.sd" "$BootMnt/boot.sd.vendor"
    echo "  + boot.sd → boot.sd.vendor"
  fi
  cp -f "$HERE/boot.sd" "$BootMnt/boot.sd"
  echo "  + boot boot.sd (FIT packed from BLOCKS/Kernel.elf)"
  cp -f "$HERE/boot.scr" "$BootMnt/boot.scr"
  echo "  + boot boot.scr (fatload mmc 0:4 Kernel.elf)"
  if [ -f "$HERE/duo.dtb" ]; then
    cp -f "$HERE/duo.dtb" "$BootMnt/duo.dtb"
    echo "  + boot duo.dtb"
  fi
  if [ -f "$BootMnt/Kernel.elf" ]; then
    rm -f "$BootMnt/Kernel.elf"
    echo "  - boot removed Kernel.elf"
  fi
fi

sync
echo "ck-duo-sd: OK — Kernel.elf only on BLOCKS; boot has no Kernel.elf"

if [ "$DO_EJECT" = 1 ]; then
  for Label in boot BLOCKS rootfs; do
    M="$(find_mnt "$Label")" || true
    if [ -n "${M:-}" ]; then
      Dev="$(findmnt -n -o SOURCE --target "$M" 2>/dev/null || true)"
      if [ -n "$Dev" ] && command -v udisksctl >/dev/null 2>&1; then
        udisksctl unmount -b "$Dev" >/dev/null 2>&1 || true
      fi
    fi
  done
  if command -v udisksctl >/dev/null 2>&1; then
    Disk="$(lsblk -ln -o NAME,LABEL | awk '$2=="BLOCKS"{print "/dev/"$1; exit}')"
    if [ -n "$Disk" ]; then
      Parent="${Disk%%[0-9]*}"
      case "$Disk" in *p[0-9]*) Parent="${Disk%p[0-9]*}" ;; esac
      udisksctl power-off -b "$Parent" >/dev/null 2>&1 || true
    fi
  fi
  echo "ck-duo-sd: ejected（可拔卡）"
fi
