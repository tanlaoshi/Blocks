#!/bin/bash
# sync-nuc.sh — 同步到 NUC SSD 上的 BLOCKS 分区（日常路径）
#
# 布局：
#   LABEL=BLOCKS（nvme 数据分区）→ Kernel.elf / RootFs / FW/ …
#   /boot/efi/EFI/Blocks/BOOTX64.EFI ← 仅 --boot 时更新（grub chainloader）
#
# 选盘：只认 nvme（或非 USB）上的 LABEL=BLOCKS；U 盘同标可插着，不误写。
# U 盘双分区仍用 Scripts/sync-usb.sh（备选）。
#
# 用法：
#   ./sync-nuc.sh                 # RootFs → BLOCKS（不动 Boot）
#   ./sync-nuc.sh --kernel-only   # 只更新 Kernel.elf + FW/
#   ./sync-nuc.sh --boot          # 另写 EFI/Blocks/BOOTX64.EFI（需可写 ESP）
#   ./sync-nuc.sh --build         # 先编 Kernel（+ Boot 若 --boot），再 prepare-rootfs，再同步
#   BLOCKS_MNT=/mnt/blocks ./sync-nuc.sh
#   ESP_BOOT_DIR=/boot/efi/EFI/Blocks ./sync-nuc.sh --boot
#
# WIFI.CFG / WIFI_H.CFG / WIFI_C.CFG：保留在 BLOCKS 上，不同步覆盖。
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=toyos-common.sh
. "$SCRIPT_DIR/toyos-common.sh"
ROOT="$(toyos_image_root "$0")"
DO_BUILD=0
KERNEL_ONLY=0
DO_BOOT=0
ESP_BOOT_DIR="${ESP_BOOT_DIR:-/boot/efi/EFI/Blocks}"

sync_blocks_fw() {
  local Dest="$1"
  local SrcFw="$ROOT/RootFs/X64/FW"
  if [[ ! -d "$SrcFw" ]]; then
    echo "warning: missing $SrcFw — iwl8265 will fw=miss until FW/ is present" >&2
    return 0
  fi
  mkdir -p "$Dest/FW"
  if command -v rsync >/dev/null 2>&1; then
    rsync -rltD --delete \
      --exclude 'WIFI.CFG' \
      --exclude 'WIFI_H.CFG' \
      --exclude 'WIFI_C.CFG' \
      --filter 'protect WIFI.CFG' \
      --filter 'protect WIFI_H.CFG' \
      --filter 'protect WIFI_C.CFG' \
      --no-owner --no-group --no-perms \
      "$SrcFw/" "$Dest/FW/"
  else
    local f base
    for f in "$SrcFw"/*; do
      [[ -e "$f" ]] || continue
      base="$(basename "$f")"
      if [[ "$base" == "WIFI.CFG" || "$base" == "WIFI_H.CFG" || "$base" == "WIFI_C.CFG" ]]; then
        continue
      fi
      cp -f "$f" "$Dest/FW/"
    done
  fi
  if [[ -f "$Dest/FW/IWL8265.UCODE" ]]; then
    echo "FW/IWL8265.UCODE -> $Dest/FW/ ($(stat -c%s "$Dest/FW/IWL8265.UCODE") bytes)"
  else
    echo "warning: $Dest/FW/ has no IWL8265.UCODE" >&2
  fi
  if [[ -f "$Dest/FW/WIFI.CFG" ]]; then
    echo "FW/WIFI.CFG kept on BLOCKS (not overwritten)"
  else
    echo "note: no FW/WIFI.CFG — cp WIFI_H.CFG or WIFI_C.CFG → WIFI.CFG"
  fi
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help)
      sed -n '2,20p' "$0" | sed 's/^# \?//'
      exit 0
      ;;
    --build|-b) DO_BUILD=1; shift ;;
    --kernel-only|-k) KERNEL_ONLY=1; shift ;;
    --boot) DO_BOOT=1; shift ;;
    *)
      echo "unknown arg: $1 (USB 备选请用 sync-usb.sh)" >&2
      exit 1
      ;;
  esac
done

# 是否 NUC 本机盘（nvme / 非 USB）；U 盘同 LABEL=BLOCKS 时排除。
is_nuc_blocks_dev() {
  local Dev="$1"
  local Tran Rm Name
  [[ -b "$Dev" ]] || return 1
  Name="$(basename "$Dev")"
  case "$Name" in
    nvme*|sd*[0-9]|vd*[0-9]|mmcblk*p*) ;;
    *) return 1 ;;
  esac
  Tran="$(lsblk -ln -o TRAN "$Dev" 2>/dev/null | awk 'NF{print; exit}')"
  if [[ -z "$Tran" ]]; then
    # 分区常无 TRAN，看父盘
    Tran="$(lsblk -ln -o TRAN -s "$Dev" 2>/dev/null | awk 'NF{print; exit}')"
  fi
  Rm="$(lsblk -ln -o RM "$Dev" 2>/dev/null | awk 'NF{print; exit}')"
  if [[ "$Tran" == "usb" || "$Rm" == "1" ]]; then
    return 1
  fi
  if [[ "$Tran" == "nvme" || "$Name" == nvme* ]]; then
    return 0
  fi
  # 非 USB 的固定盘（少见 SATA 本机）也认
  [[ -n "$Tran" && "$Tran" != "usb" ]]
}

# 枚举所有 LABEL=BLOCKS，优先 nvme。
find_blocks_dev() {
  local Dev Name Prefer="" Fallback=""
  while read -r Name; do
    [[ -n "$Name" ]] || continue
    Dev="/dev/$Name"
    [[ -b "$Dev" ]] || continue
    is_nuc_blocks_dev "$Dev" || continue
    if [[ "$(basename "$Dev")" == nvme* ]]; then
      Prefer="$Dev"
      break
    fi
    [[ -z "$Fallback" ]] && Fallback="$Dev"
  done < <(lsblk -ln -o NAME,LABEL 2>/dev/null | awk '$2=="BLOCKS"{print $1}')
  if [[ -n "$Prefer" ]]; then
    printf '%s\n' "$Prefer"
    return 0
  fi
  if [[ -n "$Fallback" ]]; then
    printf '%s\n' "$Fallback"
    return 0
  fi
  return 1
}

find_dev_mnt() {
  local Mnt
  Mnt="$(lsblk -ln -o MOUNTPOINT "$1" 2>/dev/null | awk 'NF{print; exit}')"
  if [[ -z "$Mnt" || "$Mnt" == "-" ]]; then
    return 1
  fi
  printf '%s\n' "$Mnt"
}

# /media/*/BLOCKS 可能是 U 盘；只接受挂在本机盘上的路径。
find_nuc_blocks_mnt_path() {
  local d Src
  for d in /media/*/BLOCKS /media/"${USER:-tank}"/BLOCKS \
           /run/media/"${USER:-tank}"/BLOCKS; do
    [[ -d "$d" ]] || continue
    Src="$(findmnt -n -o SOURCE --target "$d" 2>/dev/null || true)"
    [[ -n "$Src" ]] || continue
    if is_nuc_blocks_dev "$Src"; then
      printf '%s\n' "$d"
      return 0
    fi
  done
  return 1
}

ensure_blocks_mounted() {
  local Dev="$1"
  local Mnt Out
  Mnt="$(find_dev_mnt "$Dev" || true)"
  if [[ -n "$Mnt" ]]; then
    printf '%s\n' "$Mnt"
    return 0
  fi
  if command -v udisksctl >/dev/null 2>&1; then
    Out="$(udisksctl mount -b "$Dev" 2>/dev/null || true)"
    if [[ "$Out" =~ Mounted\ .*\ at\ (.+)$ ]]; then
      Mnt="${BASH_REMATCH[1]}"
      echo "mounted $Dev -> $Mnt (udisksctl)" >&2
      printf '%s\n' "$Mnt"
      return 0
    fi
  fi
  echo "error: BLOCKS at $Dev not mounted. Try: udisksctl mount -b $Dev" >&2
  return 1
}

pick_kernel_src() {
  local Bld Rfs
  Bld="$(toyos_kernel_build_dir "$(toyos_resolve_root "$0")")/HAL/X64/Kernel.elf"
  Rfs="$ROOT/RootFs/X64/Kernel.elf"
  if [[ -f "$Bld" && -f "$Rfs" ]]; then
    if [[ "$Bld" -nt "$Rfs" ]]; then
      printf '%s\n' "$Bld"
    else
      printf '%s\n' "$Rfs"
    fi
  elif [[ -f "$Bld" ]]; then
    printf '%s\n' "$Bld"
  elif [[ -f "$Rfs" ]]; then
    printf '%s\n' "$Rfs"
  else
    return 1
  fi
}

sync_boot_chainloader() {
  local BootEfi="$1"
  local DestDir="$ESP_BOOT_DIR"
  local Dest="$DestDir/BOOTX64.EFI"
  if [[ ! -f "$BootEfi" ]]; then
    echo "error: missing $BootEfi — build Boot first" >&2
    return 1
  fi
  if [[ -d "$DestDir" && -w "$DestDir" ]]; then
    cp -f "$BootEfi" "$Dest"
  elif command -v sudo >/dev/null 2>&1; then
    sudo mkdir -p "$DestDir"
    sudo cp -f "$BootEfi" "$Dest"
  else
    echo "error: cannot write $Dest (ESP 需 root)" >&2
    return 1
  fi
  sync
  local Sz
  Sz="$(stat -c%s "$Dest" 2>/dev/null || sudo stat -c%s "$Dest")"
  echo "Boot -> $Dest ($Sz bytes)"
}

BLOCKS_MNT="${BLOCKS_MNT:-}"
BLOCKS_DEV=""

if [[ -z "$BLOCKS_MNT" ]]; then
  BLOCKS_DEV="$(find_blocks_dev || true)"
  if [[ -n "$BLOCKS_DEV" ]]; then
    BLOCKS_MNT="$(find_dev_mnt "$BLOCKS_DEV" || true)"
  fi
fi
if [[ -z "$BLOCKS_MNT" ]]; then
  BLOCKS_MNT="$(find_nuc_blocks_mnt_path || true)"
fi
if [[ -z "$BLOCKS_MNT" ]]; then
  if [[ -z "$BLOCKS_DEV" ]]; then
    BLOCKS_DEV="$(find_blocks_dev || true)"
  fi
  if [[ -n "$BLOCKS_DEV" ]]; then
    BLOCKS_MNT="$(ensure_blocks_mounted "$BLOCKS_DEV" || true)"
  fi
fi
if [[ -z "$BLOCKS_MNT" ]]; then
  echo "error: NUC BLOCKS (nvme LABEL=BLOCKS) not found/mounted." >&2
  echo "  U 盘同标可插着；本脚本不写 USB。备选: sync-usb.sh" >&2
  exit 1
fi
if [[ ! -d "$BLOCKS_MNT" || ! -w "$BLOCKS_MNT" ]]; then
  echo "error: BLOCKS not writable: $BLOCKS_MNT" >&2
  exit 1
fi
# 显式 BLOCKS_MNT 也校验：勿误指 U 盘
if [[ -z "$BLOCKS_DEV" ]]; then
  BLOCKS_DEV="$(findmnt -n -o SOURCE --target "$BLOCKS_MNT" 2>/dev/null || true)"
fi
if [[ -n "$BLOCKS_DEV" ]] && ! is_nuc_blocks_dev "$BLOCKS_DEV"; then
  echo "error: $BLOCKS_MNT is $BLOCKS_DEV (USB/removable) — sync-nuc 只写本机盘" >&2
  echo "  卸 U 盘或用 sync-usb.sh；或 BLOCKS_MNT= 指向 nvme 上的 BLOCKS" >&2
  exit 1
fi
echo "NUC BLOCKS: $BLOCKS_DEV -> $BLOCKS_MNT"

BOOT_EFI="$ROOT/Esp/X64/EFI/BOOT/BOOTX64.EFI"

if [[ "$KERNEL_ONLY" -eq 1 ]]; then
  if [[ "$DO_BUILD" -eq 1 ]]; then
    echo "== build Kernel =="
    (cd "$ROOT/../Kernel" && ./build.sh)
  fi
  SRC="$(pick_kernel_src || true)"
  if [[ -z "$SRC" ]]; then
    echo "error: no Kernel.elf — ./build.sh 或去掉 --kernel-only" >&2
    exit 1
  fi
  cp -f "$SRC" "$BLOCKS_MNT/Kernel.elf"
  sync_blocks_fw "$BLOCKS_MNT"
  if [[ ! -f "$BLOCKS_MNT/BLOCKS.ID" ]]; then
    printf "Blocks root volume\n" > "$BLOCKS_MNT/BLOCKS.ID"
  fi
  if [[ "$DO_BOOT" -eq 1 ]]; then
    if [[ "$DO_BUILD" -eq 1 && -x "$ROOT/../Boot/build.sh" ]]; then
      (cd "$ROOT/../Boot" && ./build.sh)
    fi
    sync_boot_chainloader "$BOOT_EFI"
  fi
  # boot-root-cfg：ESP 可写或 --boot 时刷新指针
  if [[ -d "$ESP_BOOT_DIR" || "$DO_BOOT" -eq 1 ]]; then
    "$SCRIPT_DIR/write-root-cfg.sh" "$ESP_BOOT_DIR" "$BLOCKS_MNT" || true
  fi
  sync
  echo "=== nuc kernel-only ==="
  echo "Blocks -> $BLOCKS_MNT"
  echo "src   -> $SRC"
  ls -l --time-style=long-iso "$SRC" "$BLOCKS_MNT/Kernel.elf" "$BLOCKS_MNT/BLOCKS.ID"
  ls -l --time-style=long-iso "$BLOCKS_MNT/FW/IWL8265.UCODE" 2>/dev/null || true
  ls -l --time-style=long-iso "$BLOCKS_MNT/FW/WIFI.CFG" 2>/dev/null || true
  md5sum "$SRC" "$BLOCKS_MNT/Kernel.elf"
  exit 0
fi

if [[ "$DO_BUILD" -eq 1 ]]; then
  echo "== build Kernel =="
  (cd "$ROOT/../Kernel" && ./build.sh)
  if [[ "$DO_BOOT" -eq 1 ]]; then
    echo "== build Boot =="
    if [[ -x "$ROOT/../Boot/build.sh" ]]; then
      (cd "$ROOT/../Boot" && ./build.sh)
    else
      echo "warning: Boot/build.sh missing" >&2
    fi
  fi
fi

echo "== prepare-rootfs =="
"$SCRIPT_DIR/prepare-rootfs.sh"

echo "Blocks -> $BLOCKS_MNT"
if [[ "$DO_BOOT" -eq 1 ]]; then
  echo "== sync chainloader Boot =="
  sync_boot_chainloader "$BOOT_EFI"
fi

echo "== sync Blocks (RootFs/X64) =="
if command -v rsync >/dev/null 2>&1; then
  rsync -rltD --delete \
    --no-owner --no-group --no-perms \
    --exclude 'System Volume Information' \
    --exclude '.Trash*' \
    --exclude 'lost+found' \
    --exclude 'FW/WIFI.CFG' \
    --exclude 'FW/WIFI_H.CFG' \
    --exclude 'FW/WIFI_C.CFG' \
    --filter 'protect FW/WIFI.CFG' \
    --filter 'protect FW/WIFI_H.CFG' \
    --filter 'protect FW/WIFI_C.CFG' \
    "$ROOT/RootFs/X64/" "$BLOCKS_MNT/"
else
  echo "error: 需要 rsync。没有它时整盘拷贝会盖掉盘上已写好的 FW/WIFI.CFG" >&2
  exit 1
fi
sync_blocks_fw "$BLOCKS_MNT"

# 双分区：Boot 只在 /boot/efi/EFI/Blocks；清掉误写到数据分区的 EFI/
if [[ -d "$BLOCKS_MNT/EFI" ]]; then
  echo "note: removing stale $BLOCKS_MNT/EFI (NUC Boot is on ESP EFI/Blocks only)"
  rm -rf "$BLOCKS_MNT/EFI"
fi

if [[ ! -f "$BLOCKS_MNT/BLOCKS.ID" ]]; then
  printf "Blocks root volume\n" > "$BLOCKS_MNT/BLOCKS.ID"
fi

# boot-root-cfg：ESP 可写则更新 ROOT.CFG（--boot 或日常 sync）
if [[ -d "$ESP_BOOT_DIR" || "$DO_BOOT" -eq 1 ]]; then
  "$SCRIPT_DIR/write-root-cfg.sh" "$ESP_BOOT_DIR" "$BLOCKS_MNT" || true
fi

sync
echo
echo "=== nuc sync done ==="
[[ "$DO_BOOT" -eq 1 ]] && echo "Boot : $ESP_BOOT_DIR/BOOTX64.EFI"
echo "Kernel : $(stat -c%s "$BLOCKS_MNT/Kernel.elf") bytes @ $BLOCKS_MNT"
ls -lh "$BLOCKS_MNT/Kernel.elf" "$BLOCKS_MNT/BLOCKS.ID" 2>/dev/null || true
echo "日常只跑本脚本；U 盘备选 sync-usb.sh。Boot 有改动再加 --boot。"
