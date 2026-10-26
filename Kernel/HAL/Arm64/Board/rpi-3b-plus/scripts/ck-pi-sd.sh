#!/bin/bash
# ck-pi-sd.sh — 暗号 CK：先摆盘（原 prepare-pi-sd）再刷卡
#   固件→bootfs；Kernel/根→BLOCKS（对齐 X64；blocks-elf）
#
#   source ~/Blocks/env.sh && ck-pi-sd
#   ck-pi-sd --wait=120
#   ck-pi-sd --no-eject
#   ck-pi-sd --repart          # 整盘重做成 bootfs+BLOCKS 两 FAT32（须 sudo）
#   ck-pi-sd --dtb=PATH        # 传给摆盘
#   ck-pi-sd /path/to/out      # 摆盘 OUT（默认 Build/rpi-3b-plus-sd）
#
# 须已 build arm64 BOARD=rpi-3b-plus。不 commit / 不 push。
# 只摆盘不刷卡：仍可用 prepare-pi-sd。
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
BOARD_DIR="$(cd "$HERE/.." && pwd)"
BLOCKS_ROOT="$(cd "$BOARD_DIR/../../../../.." && pwd)"
SRC="${BLOCKS_SRC:-$BLOCKS_ROOT/Build/rpi-3b-plus-sd}"
WAIT_SEC="${CK_WAIT_SEC:-90}"
DO_EJECT=1
DO_REPART=0
PREP_FLAGS=()

for A in "$@"; do
  case "$A" in
    --no-eject) DO_EJECT=0 ;;
    --repart) DO_REPART=1 ;;
    --wait=*) WAIT_SEC="${A#--wait=}" ;;
    --dtb=*|--elf=*|--bin) PREP_FLAGS+=("$A") ;;
    -h|--help)
      sed -n '2,16p' "$0" | sed 's/^# \?//'
      exit 0
      ;;
    -*)
      echo "error: unknown arg: $A" >&2
      exit 1
      ;;
    *)
      # 位置参数 = 摆盘 OUT；刷卡 SRC 跟同一目录
      SRC="$A"
      ;;
  esac
done

echo "CK: prepare → $SRC"
bash "$HERE/prepare-sd.sh" "$SRC" "${PREP_FLAGS[@]}"

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

parent_of() {
  local Dev=$1 Parent
  Parent="${Dev%%[0-9]*}"
  case "$Dev" in
    *p[0-9]*) Parent="${Dev%p[0-9]*}" ;;
  esac
  printf '%s\n' "$Parent"
}

if [ ! -f "$SRC/Kernel.elf" ]; then
  echo "error: missing $SRC/Kernel.elf（prepare 未产出）" >&2
  exit 1
fi

echo "CK: flash SRC=$SRC  wait≤${WAIT_SEC}s  repart=$DO_REPART"
BootMnt=""
Elapsed=0
while [ "$Elapsed" -le "$WAIT_SEC" ]; do
  if BootMnt="$(find_mnt bootfs)"; then
    break
  fi
  # --repart：也可等裸盘
  if [ "$DO_REPART" = 1 ]; then
    for cand in /dev/sdc /dev/sdb; do
      [ -b "$cand" ] || continue
      if lsblk -dn -o NAME,TYPE "$cand" | grep -q disk; then
        echo "CK: repart $cand"
        bash "$HERE/repart-pi-sd.sh" "$cand"
        BootMnt="$(find_mnt bootfs)" || true
        break 2
      fi
    done
  fi
  if [ "$Elapsed" -eq 0 ]; then
    echo "CK: 等待 bootfs（插卡即刷）…"
  fi
  sleep 2
  Elapsed=$((Elapsed + 2))
done
if [ -z "$BootMnt" ]; then
  echo "error: ${WAIT_SEC}s 内未检测到 bootfs" >&2
  exit 1
fi
echo "CK: bootfs=$BootMnt"

if [ "$DO_REPART" = 1 ]; then
  BootDev="$(findmnt -n -o SOURCE --target "$BootMnt" 2>/dev/null || true)"
  if [ -n "$BootDev" ]; then
    Disk="$(parent_of "$BootDev")"
    echo "CK: --repart → $Disk"
    # 卸后重分
    udisksctl unmount -b "$BootDev" 2>/dev/null || true
    BTmp="$(find_mnt BLOCKS)" || true
    if [ -n "${BTmp:-}" ]; then
      BDev="$(findmnt -n -o SOURCE --target "$BTmp" 2>/dev/null || true)"
      [ -n "$BDev" ] && udisksctl unmount -b "$BDev" 2>/dev/null || true
    fi
    bash "$HERE/repart-pi-sd.sh" "$Disk"
    BootMnt="$(find_mnt bootfs)" || true
    if [ -z "$BootMnt" ]; then
      echo "error: repart 后无 bootfs" >&2
      exit 1
    fi
    echo "CK: bootfs=$BootMnt (after repart)"
  fi
fi

# —— bootfs：仅固件（无 Kernel.elf）——
BootFiles=(boot.scr boot.cmd pi.dtb u-boot.bin config.txt README-SD.txt NOTES-board.md)
for f in "${BootFiles[@]}"; do
  if [ -f "$SRC/$f" ]; then
    cp -f "$SRC/$f" "$BootMnt/$f"
    echo "  + bootfs $f"
  else
    echo "  · skip bootfs (缺) $f"
  fi
done
# 清掉误放在 bootfs 的内核（旧布局）
for stale in Kernel.elf Kernel.bin; do
  if [ -f "$BootMnt/$stale" ]; then
    rm -f "$BootMnt/$stale"
    echo "  - bootfs removed $stale"
  fi
done
if [ -f "$BootMnt/uboot.env" ]; then
  rm -f "$BootMnt/uboot.env"
  echo "CK: removed stale uboot.env"
fi
sync
if [ -f "$SRC/u-boot.bin" ]; then
  cmp -s "$SRC/u-boot.bin" "$BootMnt/u-boot.bin" || {
    echo "error: u-boot.bin 校验失败" >&2
    exit 1
  }
fi
if [ -f "$SRC/boot.scr" ]; then
  cmp -s "$SRC/boot.scr" "$BootMnt/boot.scr" || {
    echo "error: boot.scr 校验失败" >&2
    exit 1
  }
fi
echo "CK: bootfs OK"

# —— BLOCKS：Kernel + BLOCKS.ID + 字体 + 最小根（对齐 X64）——
BlocksMnt="$(find_mnt BLOCKS)" || true
if [ -z "${BlocksMnt:-}" ]; then
  echo "error: 无 LABEL=BLOCKS；请 ck-pi-sd --repart 或手工 mkfs.vfat -F32 -n BLOCKS" >&2
  exit 1
fi
echo "CK: BLOCKS=$BlocksMnt"

cp -f "$SRC/Kernel.elf" "$BlocksMnt/Kernel.elf"
echo "  + BLOCKS Kernel.elf"
cmp -s "$SRC/Kernel.elf" "$BlocksMnt/Kernel.elf" || {
  echo "error: BLOCKS Kernel.elf 校验失败" >&2
  exit 1
}
if [ -f "$SRC/Kernel.bin" ]; then
  cp -f "$SRC/Kernel.bin" "$BlocksMnt/Kernel.bin"
  echo "  + BLOCKS Kernel.bin"
fi

IdSrc="${BLOCKS_ROOT}/Runtime/RootFs/X64/BLOCKS.ID"
if [ -f "$IdSrc" ]; then
  cp -f "$IdSrc" "$BlocksMnt/BLOCKS.ID"
else
  printf 'Blocks root volume\n' >"$BlocksMnt/BLOCKS.ID"
fi
echo "  + BLOCKS BLOCKS.ID"

AssetRoot="${BLOCKS_ROOT}/Runtime/Assets"
# 字体
mkdir -p "$BlocksMnt/Assets/Fonts"
for f in CJK.TTF MONO.TTF VGA8X16.FNT; do
  if [ -f "$AssetRoot/Fonts/$f" ]; then
    cp -f "$AssetRoot/Fonts/$f" "$BlocksMnt/Assets/Fonts/$f"
    echo "  + BLOCKS Assets/Fonts/$f"
  fi
done
# 桌面图标：规范路径 bmp48/；并扁平一份到 Icons/（录像见过扁平摆盘，Load 双试）
if [ -d "$AssetRoot/Icons/bmp48" ]; then
  mkdir -p "$BlocksMnt/Assets/Icons/bmp48"
  cp -a "$AssetRoot/Icons/bmp48/." "$BlocksMnt/Assets/Icons/bmp48/"
  # 缺 STORE.BMP 时用 SET 顶（菜单/桌面商店槽）
  if [ ! -f "$BlocksMnt/Assets/Icons/bmp48/STORE.BMP" ] && \
     [ -f "$BlocksMnt/Assets/Icons/bmp48/SET.BMP" ]; then
    cp -f "$BlocksMnt/Assets/Icons/bmp48/SET.BMP" \
      "$BlocksMnt/Assets/Icons/bmp48/STORE.BMP"
  fi
  cp -f "$BlocksMnt/Assets/Icons/bmp48/"*.BMP "$BlocksMnt/Assets/Icons/"
  if [ ! -f "$BlocksMnt/Assets/Icons/bmp48/SHELL.BMP" ] || \
     [ ! -f "$BlocksMnt/Assets/Icons/SHELL.BMP" ]; then
    echo "CK: error — SHELL.BMP missing under Icons/ or Icons/bmp48/" >&2
    exit 1
  fi
  echo "  + BLOCKS Assets/Icons/bmp48/ + flat Icons/*.BMP"
elif [ -d "$AssetRoot/Icons" ]; then
  mkdir -p "$BlocksMnt/Assets/Icons"
  cp -a "$AssetRoot/Icons/." "$BlocksMnt/Assets/Icons/"
  echo "  + BLOCKS Assets/Icons/ (no bmp48 dir in tree)"
fi
# 壁纸（可选；无则纯色/渐变）
if [ -f "$AssetRoot/Images/WALL.BMP" ]; then
  mkdir -p "$BlocksMnt/Assets/Images"
  cp -f "$AssetRoot/Images/WALL.BMP" "$BlocksMnt/Assets/Images/WALL.BMP"
  echo "  + BLOCKS Assets/Images/WALL.BMP"
elif [ -f "$AssetRoot/Icons/bmp48/WALL.BMP" ]; then
  mkdir -p "$BlocksMnt/Assets/Images"
  cp -f "$AssetRoot/Icons/bmp48/WALL.BMP" "$BlocksMnt/Assets/Images/WALL.BMP"
  echo "  + BLOCKS Assets/Images/WALL.BMP (from Icons)"
fi

ArmRoot="${BLOCKS_ROOT}/Runtime/RootFs/Arm64"
if [ -d "$ArmRoot" ]; then
  for f in HELLO.ELF THEME.CFG; do
    if [ -f "$ArmRoot/$f" ]; then
      cp -f "$ArmRoot/$f" "$BlocksMnt/$f"
      echo "  + BLOCKS $f"
    fi
  done
fi
sync
echo "CK: BLOCKS OK"

if [ "$DO_EJECT" = 1 ]; then
  BootDev="$(findmnt -n -o SOURCE --target "$BootMnt" 2>/dev/null || true)"
  BDev="$(findmnt -n -o SOURCE --target "$BlocksMnt" 2>/dev/null || true)"
  if command -v udisksctl >/dev/null 2>&1; then
    [ -n "$BootDev" ] && udisksctl unmount -b "$BootDev" 2>/dev/null || true
    [ -n "$BDev" ] && udisksctl unmount -b "$BDev" 2>/dev/null || true
    if [ -n "$BootDev" ]; then
      udisksctl power-off -b "$(parent_of "$BootDev")" 2>/dev/null || true
    fi
  else
    sync
    echo "CK: 无 udisksctl，已 sync；请手动弹出"
  fi
  echo "CK done（已弹出）"
else
  echo "CK done（未弹出）"
fi
