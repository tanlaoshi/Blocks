#!/bin/bash
# ck-pi-sd.sh — 暗号 CK：Build/Pi3BPlus-sd → LABEL=bootfs，sync 后安全弹出
#
#   source ~/Blocks/env.sh && ck-pi-sd
#   ck-pi-sd --wait=120          # 无卡时最多等 N 秒（默认 90）
#   ck-pi-sd --no-eject          # 只刷不弹出
#
# 有卡即刷；暂无则轮询等到 bootfs 挂上（「以后有卡自动刷」）。
# 不 commit / 不 push。
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
BOARD_DIR="$(cd "$HERE/.." && pwd)"
BLOCKS_ROOT="$(cd "$BOARD_DIR/../../../../../.." && pwd)"
SRC="${BLOCKS_SRC:-$BLOCKS_ROOT/Build/Pi3BPlus-sd}"
WAIT_SEC="${CK_WAIT_SEC:-90}"
DO_EJECT=1

for A in "$@"; do
  case "$A" in
    --no-eject) DO_EJECT=0 ;;
    --wait=*) WAIT_SEC="${A#--wait=}" ;;
    -h|--help)
      sed -n '2,12p' "$0" | sed 's/^# \?//'
      exit 0
      ;;
    *)
      echo "error: unknown arg: $A" >&2
      exit 1
      ;;
  esac
done

find_bootfs() {
  local d
  for d in \
    /media/"${USER:-tank}"/bootfs \
    /run/media/"${USER:-tank}"/bootfs \
    /media/*/bootfs; do
    if [ -d "$d" ] && [ -w "$d" ]; then
      printf '%s\n' "$d"
      return 0
    fi
  done
  # 已插未挂：试 udisksctl
  local Dev
  Dev="$(lsblk -ln -o NAME,LABEL | awk '$2=="bootfs"{print "/dev/"$1; exit}')"
  if [ -n "$Dev" ] && command -v udisksctl >/dev/null 2>&1; then
    udisksctl mount -b "$Dev" >/dev/null 2>&1 || true
    for d in /media/"${USER:-tank}"/bootfs /run/media/"${USER:-tank}"/bootfs; do
      if [ -d "$d" ] && [ -w "$d" ]; then
        printf '%s\n' "$d"
        return 0
      fi
    done
  fi
  return 1
}

if [ ! -d "$SRC" ]; then
  echo "error: missing $SRC（先 prepare-pi-sd）" >&2
  exit 1
fi
if [ ! -f "$SRC/Kernel.elf" ]; then
  echo "error: missing $SRC/Kernel.elf" >&2
  exit 1
fi

echo "CK: SRC=$SRC  wait≤${WAIT_SEC}s"
DST=""
Elapsed=0
while [ "$Elapsed" -le "$WAIT_SEC" ]; do
  if DST="$(find_bootfs)"; then
    break
  fi
  if [ "$Elapsed" -eq 0 ]; then
    echo "CK: 等待 bootfs（插卡即刷）…"
  fi
  sleep 2
  Elapsed=$((Elapsed + 2))
done
if [ -z "$DST" ]; then
  echo "error: ${WAIT_SEC}s 内未检测到 bootfs" >&2
  exit 1
fi
echo "CK: bootfs=$DST"

# 必测：内核 / 启动脚本 / DTB / U-Boot（自动进 boot.scr 依赖 stock u-boot.bin）
Files=(Kernel.elf Kernel.bin boot.scr boot.cmd pi.dtb u-boot.bin config.txt README-SD.txt NOTES-board.md)
for f in "${Files[@]}"; do
  if [ -f "$SRC/$f" ]; then
    cp -f "$SRC/$f" "$DST/$f"
    echo "  + $f"
  else
    echo "  · skip (缺) $f"
  fi
done
sync
cmp -s "$SRC/Kernel.elf" "$DST/Kernel.elf" || { echo "error: Kernel.elf 校验失败" >&2; exit 1; }
if [ -f "$SRC/u-boot.bin" ]; then
  cmp -s "$SRC/u-boot.bin" "$DST/u-boot.bin" || { echo "error: u-boot.bin 校验失败" >&2; exit 1; }
fi
echo "CK: 校验 OK"

if [ "$DO_EJECT" = 1 ]; then
  # 卸 bootfs + BLOCKS，再对整盘 power-off
  BootDev="$(findmnt -n -o SOURCE --target "$DST" 2>/dev/null || true)"
  BlocksMnt=""
  for d in /media/"${USER:-tank}"/BLOCKS /run/media/"${USER:-tank}"/BLOCKS; do
    [ -d "$d" ] && BlocksMnt=$d && break
  done
  if command -v udisksctl >/dev/null 2>&1; then
    [ -n "$BootDev" ] && udisksctl unmount -b "$BootDev" 2>/dev/null || true
    if [ -n "$BlocksMnt" ]; then
      BDev="$(findmnt -n -o SOURCE --target "$BlocksMnt" 2>/dev/null || true)"
      [ -n "$BDev" ] && udisksctl unmount -b "$BDev" 2>/dev/null || true
    fi
    # 父盘：/dev/sda1 → /dev/sda
    if [ -n "$BootDev" ]; then
      Parent="${BootDev%%[0-9]*}"
      # nvme 风格 …p1
      case "$BootDev" in
        *p[0-9]*) Parent="${BootDev%p[0-9]*}" ;;
      esac
      udisksctl power-off -b "$Parent" 2>/dev/null || true
    fi
  else
    sync
    echo "CK: 无 udisksctl，已 sync；请手动弹出"
  fi
  echo "CK done（已弹出）"
else
  echo "CK done（未弹出）"
fi
