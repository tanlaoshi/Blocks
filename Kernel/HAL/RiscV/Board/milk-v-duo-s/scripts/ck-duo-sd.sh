#!/bin/bash
# ck-duo-sd.sh — Duo 刷卡：Kernel 只在 LABEL=BLOCKS（对齐 X64/Pi）
#
#   source ~/Blocks/env.sh && ck-duo-sd
#   ck-duo-sd --blocks-only     # 只更新 BLOCKS/Kernel.elf（上电仍用已刷 embed FIT）
#   ck-duo-sd --script-fit      # 试 FIT type=script（真机已证厂商 bootm 不跑脚本）
#   ck-duo-sd --wait=120
#   ck-duo-sd --no-eject
#
# 默认：BLOCKS 唯一 Kernel.elf；boot 删 Kernel.elf；刷新
# boot.sd（FIT 内嵌核，sdboot 可 bootm）、boot.scr、duo.dtb。不删 fip.bin。
# duo-fatload：脚本 FIT 真机失败；上电改核仍须重打 embed FIT（或待 stub）。
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
SCRIPT_FIT=0

for A in "$@"; do
  case "$A" in
    --no-eject) DO_EJECT=0 ;;
    --blocks-only) BLOCKS_ONLY=1 ;;
    --script-fit) SCRIPT_FIT=1 ;;
    --embed-kernel) SCRIPT_FIT=0 ;; # 兼容旧口令：默认即 embed
    --boot) ;; # 兼容旧口令：默认已含 boot 刷新
    --wait=*) WAIT_SEC="${A#--wait=}" ;;
    -h|--help)
      sed -n '2,18p' "$0" | sed 's/^# \?//'
      exit 0
      ;;
    *)
      echo "error: unknown arg: $A" >&2
      exit 1
      ;;
  esac
done

# 盘名：/dev/sda1→sda；/dev/mmcblk0p1→mmcblk0；/dev/nvme0n1p3→nvme0n1
disk_parent() {
  local Dev=$1 Name
  Name="${Dev#/dev/}"
  case "$Name" in
    nvme*n*p*|mmcblk*p*) printf '%s\n' "${Name%p*}" ;;
    *) printf '%s\n' "${Name%%[0-9]*}" ;;
  esac
}

# Duo boot = 可写挂载且含 fip.bin（勿与本机 EFI 混淆）
find_duo_boot() {
  local d
  for d in \
    /media/"${USER:-tank}"/boot \
    /run/media/"${USER:-tank}"/boot \
    /media/*/boot; do
    if [ -f "$d/fip.bin" ] && [ -w "$d" ]; then
      printf '%s\n' "$d"
      return 0
    fi
  done
  return 1
}

# 与 Duo boot 同盘的 LABEL=BLOCKS（避开本机 nvme 上的 BLOCKS）
find_duo_blocks() {
  local BootMnt=$1 BootDev Parent Name Lbl Part Mnt
  BootDev="$(findmnt -n -o SOURCE --target "$BootMnt" 2>/dev/null || true)"
  [ -n "$BootDev" ] || return 1
  Parent="$(disk_parent "$BootDev")"
  while read -r Name Lbl; do
    [ "$Lbl" = "BLOCKS" ] || continue
    case "$Name" in
      "$Parent"p[0-9]*|"$Parent"[0-9]*) ;;
      *) continue ;;
    esac
    Part="/dev/$Name"
    Mnt="$(findmnt -n -o TARGET --source "$Part" 2>/dev/null || true)"
    if [ -z "$Mnt" ] && command -v udisksctl >/dev/null 2>&1; then
      udisksctl mount -b "$Part" >/dev/null 2>&1 || true
      Mnt="$(findmnt -n -o TARGET --source "$Part" 2>/dev/null || true)"
    fi
    if [ -n "$Mnt" ] && [ -w "$Mnt" ]; then
      printf '%s\n' "$Mnt"
      return 0
    fi
  done < <(lsblk -ln -o NAME,LABEL)
  return 1
}

if [ ! -f "$ELF" ]; then
  echo "error: missing $ELF（先: build riscv BOARD=milk-v-duo-s）" >&2
  exit 1
fi

# 防误刷：Build/HAL/RiscV 被 BOARD=virt 覆盖后上电会卡在 Starting kernel …
STAMP="$BLOCKS_ROOT/Kernel/Build/HAL/RiscV/.toy_board"
if [ -f "$STAMP" ]; then
  _Board="$(tr -d '\n' <"$STAMP" || true)"
  if [ "$_Board" != "milk-v-duo-s" ]; then
    echo "error: .toy_board='$_Board'（不是 milk-v-duo-s）→ 先: build riscv BOARD=milk-v-duo-s" >&2
    exit 1
  fi
fi
unset _Board

echo "ck-duo-sd: ELF=$ELF  wait≤${WAIT_SEC}s  blocks_only=$BLOCKS_ONLY"

BootMnt=""
BlocksMnt=""
Elapsed=0
while [ "$Elapsed" -le "$WAIT_SEC" ]; do
  if BootMnt="$(find_duo_boot)" && BlocksMnt="$(find_duo_blocks "$BootMnt")"; then
    break
  fi
  BootMnt=""
  BlocksMnt=""
  if [ "$Elapsed" -eq 0 ]; then
    echo "ck-duo-sd: 等待 Duo 卡（boot+fip.bin 与同盘 BLOCKS；勿与本机 BLOCKS 混淆）…"
  fi
  sleep 2
  Elapsed=$((Elapsed + 2))
done
if [ -z "${BootMnt:-}" ] || [ -z "${BlocksMnt:-}" ]; then
  echo "error: ${WAIT_SEC}s 内未检测到 Duo 卡（须 LABEL=boot 含 fip.bin + 同盘 LABEL=BLOCKS）" >&2
  exit 1
fi
echo "ck-duo-sd: boot=$BootMnt"
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

if [ -f "$BootMnt/Kernel.elf" ]; then
  rm -f "$BootMnt/Kernel.elf"
  echo "  - boot removed Kernel.elf"
fi

if [ "$BLOCKS_ONLY" = 0 ]; then
  echo "ck-duo-sd: 刷 boot FIT/scr/dtb"
  if [ "$SCRIPT_FIT" = 1 ]; then
    python3 "$HERE/mk-boot-sd.py" --script -o "$HERE/boot.sd"
    FitNote="FIT script (stock bootm FAILS — archaeology)"
  else
    python3 "$HERE/mk-boot-sd.py" -k "$BlocksMnt/Kernel.elf" -o "$HERE/boot.sd"
    FitNote="FIT embed kernel"
  fi
  python3 "$HERE/mk-boot-scr.py"
  if [ ! -f "$BootMnt/boot.sd.vendor" ] && [ -f "$BootMnt/boot.sd" ]; then
    mv "$BootMnt/boot.sd" "$BootMnt/boot.sd.vendor"
    echo "  + boot.sd → boot.sd.vendor"
  fi
  cp -f "$HERE/boot.sd" "$BootMnt/boot.sd"
  echo "  + boot boot.sd ($FitNote)"
  cp -f "$HERE/boot.scr" "$BootMnt/boot.scr"
  echo "  + boot boot.scr (抢壳 fatload mmc 0:4)"
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
if [ "$BLOCKS_ONLY" = 1 ]; then
  echo "ck-duo-sd: OK — BLOCKS/Kernel.elf only（须已刷脚本 FIT；上电勿再打 FIT）"
else
  echo "ck-duo-sd: OK — Kernel.elf only on BLOCKS; boot has no Kernel.elf"
fi

if [ "$DO_EJECT" = 1 ]; then
  BootDev="$(findmnt -n -o SOURCE --target "$BootMnt" 2>/dev/null || true)"
  ParentName=""
  if [ -n "$BootDev" ]; then
    ParentName="$(disk_parent "$BootDev")"
  fi
  for M in "$BootMnt" "$BlocksMnt"; do
    Dev="$(findmnt -n -o SOURCE --target "$M" 2>/dev/null || true)"
    if [ -n "$Dev" ] && command -v udisksctl >/dev/null 2>&1; then
      udisksctl unmount -b "$Dev" >/dev/null 2>&1 || true
    fi
  done
  if [ -n "$ParentName" ] && command -v udisksctl >/dev/null 2>&1; then
    udisksctl power-off -b "/dev/$ParentName" >/dev/null 2>&1 || true
  fi
  echo "ck-duo-sd: ejected（可拔卡）"
fi
