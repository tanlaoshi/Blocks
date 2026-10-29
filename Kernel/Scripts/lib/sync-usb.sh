#!/bin/bash
# sync-usb.sh — 把构建产物同步到 U 盘上的 ESP + BLOCKS 分区
#
# 真机布局（见 make-usb-stick.sh）：
#   ESP   → EFI/BOOT/BOOTX64.EFI
#   BLOCKS → RootFs/X64/（Kernel.elf、BLOCKS.ID、THEME、Assets、FW/、*.ELF…）
#
# 选盘：只认 USB/可移动上的卷标；NUC nvme 同标（LABEL=BLOCKS）不写。本机盘用 sync-nuc.sh。
#
# 用法：
#   ./sync-usb.sh                 # 全量：RootFs/X64 → BLOCKS；ESP 已挂载才更新 BOOTX64.EFI
#   ./sync-usb.sh --build         # 先编 Kernel + Boot，再 prepare-rootfs，再全量同步
#   ./sync-usb.sh --kernel-only   # BLOCKS/Kernel.elf + FW/ + Assets/Icons/bmp48
#   ESP_MNT=/mnt/blocks-esp BLOCKS_MNT=/mnt/blocks-data ./sync-usb.sh
#
# 兼容旧单分区：若只有 BLOCKS、没有 ESP，则把 EFI 也写进 BLOCKS（单 FAT 布局）。
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=blocks-common.sh
. "$SCRIPT_DIR/blocks-common.sh"
ROOT="$(blocks_image_root "$0")"
DO_BUILD=0
KERNEL_ONLY=0

# BLOCKS 卷上的无线固件目录（PR-N-wifi-1：FW/IWL8265.UCODE）
# WIFI.CFG / WIFI_H.CFG / WIFI_C.CFG 含 PSK：只活在 U 盘上；同步时保留、不覆盖、不 --delete。
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
    # 无 rsync：逐文件拷；已有 WIFI*.CFG 不覆盖
    local f
    for f in "$SrcFw"/*; do
      [[ -e "$f" ]] || continue
      local base
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
    echo "FW/WIFI.CFG kept on BLOCKS (not overwritten by sync)"
  else
    echo "note: no FW/WIFI.CFG on BLOCKS — cp WIFI_H.CFG or WIFI_C.CFG → WIFI.CFG"
  fi
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help)
      sed -n '2,16p' "$0" | sed 's/^# \?//'
      exit 0
      ;;
    --build|-b) DO_BUILD=1; shift ;;
    --kernel-only|-k) KERNEL_ONLY=1; shift ;;
    *)
      echo "unknown arg: $1" >&2
      exit 1
      ;;
  esac
done

# U 盘 / 可移动；排除 nvme 本机盘（与 sync-nuc 对仗）。
is_usb_stick_dev() {
  local Dev="$1"
  local Tran Rm Name
  [[ -b "$Dev" ]] || return 1
  Name="$(basename "$Dev")"
  case "$Name" in
    nvme*) return 1 ;;
  esac
  Tran="$(lsblk -ln -o TRAN "$Dev" 2>/dev/null | awk 'NF{print; exit}')"
  if [[ -z "$Tran" ]]; then
    Tran="$(lsblk -ln -o TRAN -s "$Dev" 2>/dev/null | awk 'NF{print; exit}')"
  fi
  Rm="$(lsblk -ln -o RM "$Dev" 2>/dev/null | awk 'NF{print; exit}')"
  if [[ "$Tran" == "nvme" ]]; then
    return 1
  fi
  [[ "$Tran" == "usb" || "$Rm" == "1" ]]
}

# 枚举 LABEL=…，只返回 USB/可移动上的那个（勿用 blkid -L，双标会跳到 nvme）。
find_label_dev() {
  local Label="$1"
  local Dev Name
  while read -r Name; do
    [[ -n "$Name" ]] || continue
    Dev="/dev/$Name"
    [[ -b "$Dev" ]] || continue
    is_usb_stick_dev "$Dev" || continue
    printf '%s\n' "$Dev"
    return 0
  done < <(lsblk -ln -o NAME,LABEL 2>/dev/null | awk -v L="$Label" '$2==L{print $1}')
  return 1
}

find_label_mnt() {
  local Label="$1"
  local Dev Mnt
  Dev="$(find_label_dev "$Label" || true)"
  if [[ -z "$Dev" ]]; then
    return 1
  fi
  Mnt="$(lsblk -ln -o MOUNTPOINT "$Dev" 2>/dev/null | awk 'NF{print; exit}')"
  if [[ -z "$Mnt" || "$Mnt" == "-" ]]; then
    return 1
  fi
  printf '%s\n' "$Mnt"
}

# /media/*/BLOCKS 等可能是 NUC；只接受挂在 U 盘上的路径。
find_usb_mnt_path() {
  local Label="$1"
  local d Src
  for d in /media/*/"$Label" /media/"${USER:-tank}"/"$Label" \
           /run/media/"${USER:-tank}"/"$Label"; do
    [[ -d "$d" ]] || continue
    Src="$(findmnt -n -o SOURCE --target "$d" 2>/dev/null || true)"
    [[ -n "$Src" ]] || continue
    if is_usb_stick_dev "$Src"; then
      printf '%s\n' "$d"
      return 0
    fi
  done
  return 1
}

require_usb_mnt() {
  local Mnt="$1"
  local What="$2"
  local Src
  Src="$(findmnt -n -o SOURCE --target "$Mnt" 2>/dev/null || true)"
  if [[ -z "$Src" ]]; then
    echo "error: $What mount $Mnt has no block source" >&2
    return 1
  fi
  if ! is_usb_stick_dev "$Src"; then
    echo "error: $What at $Mnt is $Src (not USB) — sync-usb 不写本机盘" >&2
    echo "  插 U 盘或用 sync-nuc.sh；勿把 BLOCKS_MNT 指到 nvme" >&2
    return 1
  fi
  echo "USB $What: $Src -> $Mnt"
  return 0
}


# 桌面常只挂数据卷、不挂 ESP；UEFI 却从 ESP 启动 → 只改数据卷等于白改。
# 优先 udisksctl（免 root，挂到 /media/$USER/…）；再试 /mnt/blocks-*。
ensure_label_mounted() {
  local Label="$1"
  local DefaultMnt="$2"
  local Dev Mnt MUID MGID Out
  Dev="$(find_label_dev "$Label")"
  if [[ -z "$Dev" ]]; then
    return 1
  fi
  Mnt="$(lsblk -ln -o MOUNTPOINT "$Dev" 2>/dev/null | awk 'NF{print; exit}')"
  if [[ -n "$Mnt" && "$Mnt" != "-" ]]; then
    printf '%s\n' "$Mnt"
    return 0
  fi
  if command -v udisksctl >/dev/null 2>&1; then
    Out="$(udisksctl mount -b "$Dev" 2>/dev/null || true)"
    if [[ "$Out" =~ Mounted\ .*\ at\ (.+)$ ]]; then
      Mnt="${BASH_REMATCH[1]}"
      echo "mounted LABEL=$Label $Dev -> $Mnt (udisksctl)" >&2
      printf '%s\n' "$Mnt"
      return 0
    fi
    # 已挂上但 udisks 只回 AlreadyMounted
    Mnt="$(lsblk -ln -o MOUNTPOINT "$Dev" 2>/dev/null | awk 'NF{print; exit}')"
    if [[ -n "$Mnt" && "$Mnt" != "-" ]]; then
      printf '%s\n' "$Mnt"
      return 0
    fi
  fi
  MUID="$(id -u)"
  MGID="$(id -g)"
  if mkdir -p "$DefaultMnt" 2>/dev/null && \
     mount -o "uid=${MUID},gid=${MGID},umask=022" "$Dev" "$DefaultMnt" 2>/dev/null; then
    echo "mounted LABEL=$Label $Dev -> $DefaultMnt" >&2
    printf '%s\n' "$DefaultMnt"
    return 0
  fi
  if command -v sudo >/dev/null 2>&1 && \
     sudo mkdir -p "$DefaultMnt" && \
     sudo mount -o "uid=${MUID},gid=${MGID},umask=022" "$Dev" "$DefaultMnt" 2>/dev/null; then
    echo "mounted LABEL=$Label $Dev -> $DefaultMnt (sudo)" >&2
    printf '%s\n' "$DefaultMnt"
    return 0
  fi
  echo "error: found LABEL=$Label at $Dev but cannot mount." >&2
  echo "  Try: udisksctl mount -b $Dev" >&2
  return 1
}

ESP_MNT="${ESP_MNT:-}"
BLOCKS_MNT="${BLOCKS_MNT:-}"


if [[ -z "$ESP_MNT" ]]; then
  ESP_MNT="$(find_label_mnt ESP || true)"
fi
if [[ -z "$ESP_MNT" ]]; then
  ESP_MNT="$(find_label_mnt EFI || true)"
fi
if [[ -z "$BLOCKS_MNT" ]]; then
  BLOCKS_MNT="$(find_label_mnt BLOCKS || true)"
fi

# 常见自动挂载路径兜底（须 USB，跳过 nvme 上的同名挂载）
if [[ -z "$BLOCKS_MNT" ]]; then
  BLOCKS_MNT="$(find_usb_mnt_path BLOCKS || true)"
fi
if [[ -z "$ESP_MNT" ]]; then
  ESP_MNT="$(find_usb_mnt_path ESP || true)"
fi
if [[ -z "$ESP_MNT" ]]; then
  ESP_MNT="$(find_usb_mnt_path EFI || true)"
fi

# 未挂载则尝试挂上（尤其 ESP：否则 UEFI 仍跑旧 BOOTX64）
if [[ -z "$ESP_MNT" ]]; then
  ESP_MNT="$(ensure_label_mounted ESP /mnt/blocks-esp || true)"
fi
if [[ -z "$ESP_MNT" ]]; then
  ESP_MNT="$(ensure_label_mounted EFI /mnt/blocks-esp || true)"
fi
if [[ -z "$BLOCKS_MNT" ]]; then
  BLOCKS_MNT="$(ensure_label_mounted BLOCKS /mnt/blocks-data || true)"
fi

if [[ -z "$BLOCKS_MNT" ]]; then
  echo "error: USB BLOCKS not found/mounted." >&2
  echo "  插 U 盘；本机 nvme LABEL=BLOCKS 用 sync-nuc.sh" >&2
  exit 1
fi
require_usb_mnt "$BLOCKS_MNT" "BLOCKS" || exit 1
if [[ -n "$ESP_MNT" && "$ESP_MNT" != "$BLOCKS_MNT" ]]; then
  require_usb_mnt "$ESP_MNT" "ESP" || exit 1
fi

# --kernel-only：只碰 BLOCKS，不要求 ESP（避免误判 single-FAT / 往 ESP 写 Kernel）
if [[ "$KERNEL_ONLY" -eq 1 ]]; then
  if [[ "$DO_BUILD" -eq 1 ]]; then
    echo "== build Kernel =="
    (cd "$ROOT/Kernel" && ./build.sh)
  fi
  if [[ ! -d "$BLOCKS_MNT" ]]; then
    echo "error: BLOCKS mount missing: $BLOCKS_MNT" >&2
    exit 1
  fi
  if [[ ! -w "$BLOCKS_MNT" ]]; then
    echo "error: BLOCKS not writable: $BLOCKS_MNT (try: sudo chown \$USER \"$BLOCKS_MNT\" or sudo $0 --kernel-only)" >&2
    exit 1
  fi
  SRC=""
  for C in \
    "$(blocks_kernel_build_dir "$(blocks_resolve_root "$0")")/HAL/X64/Kernel.elf" \
    "$ROOT/RootFs/X64/Kernel.elf"
  do
    if [[ -f "$C" ]]; then SRC="$C"; break; fi
  done
  if [[ -z "$SRC" ]]; then
    echo "error: no Kernel.elf — build first or drop --kernel-only" >&2
    exit 1
  fi
  if [[ -d "$BLOCKS_MNT/EFI/BOOT" && ! -f "$BLOCKS_MNT/BLOCKS.ID" ]]; then
    echo "error: $BLOCKS_MNT looks like ESP (EFI/BOOT present, no BLOCKS.ID)." >&2
    echo "  Use the data partition (LABEL=BLOCKS), e.g. /media/\$USER/BLOCKS" >&2
    exit 1
  fi
  cp -f "$SRC" "$BLOCKS_MNT/Kernel.elf"
  sync_blocks_fw "$BLOCKS_MNT"
  # 图标不在内核里。快同步也带上 bmp48，否则 GAME.BMP 等要等全量镜像。
  if [[ -d "$ROOT/RootFs/X64/Assets/Icons/bmp48" ]]; then
    mkdir -p "$BLOCKS_MNT/Assets/Icons/bmp48"
    if command -v rsync >/dev/null 2>&1; then
      rsync -rltD --no-owner --no-group --no-perms \
        "$ROOT/RootFs/X64/Assets/Icons/bmp48/" "$BLOCKS_MNT/Assets/Icons/bmp48/"
    else
      cp -f "$ROOT/RootFs/X64/Assets/Icons/bmp48/"*.BMP "$BLOCKS_MNT/Assets/Icons/bmp48/"
    fi
    echo "Assets/Icons/bmp48 -> $BLOCKS_MNT/Assets/Icons/bmp48"
  fi
  if [[ ! -f "$BLOCKS_MNT/BLOCKS.ID" ]]; then
    printf "Blocks root volume\n" > "$BLOCKS_MNT/BLOCKS.ID"
    echo "note: wrote $BLOCKS_MNT/BLOCKS.ID (UEFI Boot 靠它认系统盘)"
  fi
  sync
  echo "=== kernel-only sync (BLOCKS only; ESP untouched) ==="
  echo "Blocks -> $BLOCKS_MNT"
  echo "src   -> $SRC"
  ls -l --time-style=long-iso "$SRC" "$BLOCKS_MNT/Kernel.elf" "$BLOCKS_MNT/BLOCKS.ID"
  ls -l --time-style=long-iso "$BLOCKS_MNT/FW/IWL8265.UCODE" 2>/dev/null || \
    echo "note: FW/IWL8265.UCODE missing on BLOCKS (wifi fw=miss)"
  ls -l --time-style=long-iso "$BLOCKS_MNT/FW/WIFI.CFG" 2>/dev/null || \
    echo "note: FW/WIFI.CFG missing — cfg=miss until you create it on the stick"
  md5sum "$SRC" "$BLOCKS_MNT/Kernel.elf"
  echo
  echo "Boot loads Kernel from BLOCKS via BLOCKS.ID. Do not put Kernel.elf on ESP."
  echo "PHOTO 'fs: default=ESP (no BLOCKS.ID)' is often the PC's NVMe ESP — kernel may not see USB yet."
  exit 0
fi

if [[ ! -d "$BLOCKS_MNT" || ! -w "$BLOCKS_MNT" ]]; then
  echo "error: BLOCKS mount not writable: $BLOCKS_MNT" >&2
  exit 1
fi

SINGLE_FAT=0
SKIP_ESP=0
ESP_DEV="$(find_label_dev ESP)"
if [[ -z "$ESP_DEV" ]]; then
  ESP_DEV="$(find_label_dev EFI)"
fi
# 桌面常把 ESP 挂在 /mnt/blocks-esp，但 blkid 无 LABEL=ESP（仍须 USB）
if [[ -z "$ESP_MNT" && -d /mnt/blocks-esp/EFI/BOOT ]]; then
  if require_usb_mnt /mnt/blocks-esp "ESP" 2>/dev/null; then
    ESP_MNT=/mnt/blocks-esp
    echo "note: using /mnt/blocks-esp as ESP (no LABEL=ESP in blkid)"
  fi
fi
if [[ -z "$ESP_MNT" ]]; then
  if [[ -n "$ESP_DEV" ]]; then
    echo "warning: USB ESP/EFI ($ESP_DEV) is not mounted — bootloader unchanged." >&2
    echo "  BLOCKS still gets a full RootFs sync." >&2
    echo "  To update BOOTX64.EFI: sudo mount $ESP_DEV /mnt/blocks-esp && ESP_MNT=/mnt/blocks-esp $0" >&2
    SKIP_ESP=1
  elif lsblk -n -o NAME,MOUNTPOINT 2>/dev/null | grep -q blocks-esp; then
    echo "warning: dual-partition USB but ESP mount not resolved — bootloader unchanged." >&2
    echo "  BLOCKS still gets a full RootFs sync. Or: ESP_MNT=/mnt/blocks-esp $0" >&2
    SKIP_ESP=1
  else
    SINGLE_FAT=1
    ESP_MNT="$BLOCKS_MNT"
    echo "note: no ESP partition — writing EFI into BLOCKS (single-FAT layout)"
  fi
elif [[ ! -w "$ESP_MNT" ]]; then
  echo "warning: ESP not writable ($ESP_MNT) — bootloader unchanged, BLOCKS still full-synced." >&2
  SKIP_ESP=1
fi

echo "ESP   -> $ESP_MNT"
echo "Blocks -> $BLOCKS_MNT"

if [[ "$DO_BUILD" -eq 1 ]]; then
  echo "== build Kernel =="
  (cd "$ROOT/Kernel" && ./build.sh)
  echo "== build Boot =="
  if [[ -x "$ROOT/Boot/build.sh" ]]; then
    (cd "$ROOT/Boot" && ./build.sh)
  else
    echo "warning: Boot/build.sh missing; keep existing BOOTX64.EFI" >&2
  fi
fi

echo "== prepare-rootfs =="
"$SCRIPT_DIR/prepare-rootfs.sh"

BOOT_EFI="$ROOT/Esp/X64/EFI/BOOT/BOOTX64.EFI"
if [[ "$SKIP_ESP" -eq 0 && ! -f "$BOOT_EFI" ]]; then
  echo "error: missing $BOOT_EFI — build Boot" >&2
  exit 1
fi

if [[ "$SKIP_ESP" -eq 0 && "$SINGLE_FAT" -eq 0 ]]; then
  echo "== sync ESP (Boot) =="
  mkdir -p "$ESP_MNT/EFI/BOOT" "$ESP_MNT/EFI/Blocks"
  cp -f "$BOOT_EFI" "$ESP_MNT/EFI/BOOT/BOOTX64.EFI"
  sync
fi

echo "== sync BLOCKS (full RootFs/X64) =="
# FAT 无 Unix owner/mode；勿用纯 -a（会 chown 失败 → exit 23）
# 整卷镜像。WIFI*.CFG 留在 U 盘上，不覆盖、不删除。
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
  echo "error: rsync is required for a full BLOCKS mirror" >&2
  exit 1
fi
# 显式再扫一眼 FW/（rsync 已含；WIFI*.CFG 在 U 盘侧保留）
sync_blocks_fw "$BLOCKS_MNT"

if [[ "$SINGLE_FAT" -eq 1 ]]; then
  # 仅单分区盘：启动与系统同卷，EFI 只能放 BLOCKS
  mkdir -p "$BLOCKS_MNT/EFI/BOOT" "$BLOCKS_MNT/EFI/Blocks"
  cp -f "$BOOT_EFI" "$BLOCKS_MNT/EFI/BOOT/BOOTX64.EFI"
elif [[ "$SKIP_ESP" -eq 0 && -d "$BLOCKS_MNT/EFI" ]]; then
  # 双分区且这次写过 ESP：清掉 BLOCKS 上误放的 EFI（UEFI 不读它）
  echo "note: removing stale $BLOCKS_MNT/EFI (Boot lives on ESP only)"
  rm -rf "$BLOCKS_MNT/EFI"
fi

# 确保识别文件存在
if [[ ! -f "$BLOCKS_MNT/BLOCKS.ID" ]]; then
  printf "Blocks root volume\n" > "$BLOCKS_MNT/BLOCKS.ID"
fi

# boot-root-cfg：ESP（或单 FAT）写 ROOT.CFG 指向本 BLOCKS 分区
if [[ "$SINGLE_FAT" -eq 1 ]]; then
  "$SCRIPT_DIR/write-root-cfg.sh" "$BLOCKS_MNT/EFI/Blocks" "$BLOCKS_MNT" || true
elif [[ "$SKIP_ESP" -eq 0 && -n "${ESP_MNT:-}" ]]; then
  mkdir -p "$ESP_MNT/EFI/Blocks"
  "$SCRIPT_DIR/write-root-cfg.sh" "$ESP_MNT/EFI/Blocks" "$BLOCKS_MNT" || true
fi

sync
echo
echo "=== sync done (full RootFs) ==="
if [[ "$SKIP_ESP" -eq 0 ]]; then
  echo "ESP BOOTX64.EFI : $(stat -c%s "$ESP_MNT/EFI/BOOT/BOOTX64.EFI") bytes"
else
  echo "ESP BOOTX64.EFI : unchanged (ESP not mounted)"
fi
echo "BLOCKS Kernel    : $(stat -c%s "$BLOCKS_MNT/Kernel.elf") bytes"
ls -lh "$BLOCKS_MNT/Assets/Fonts/CJK.TTF" 2>/dev/null || \
  echo "warning: BLOCKS missing Assets/Fonts/CJK.TTF (Boot: ttf miss)"
ls -lh "$BLOCKS_MNT/Kernel.elf" "$BLOCKS_MNT/BLOCKS.ID" "$BLOCKS_MNT/THEME.CFG" 2>/dev/null || true
ls -lh "$BLOCKS_MNT/FW/IWL8265.UCODE" 2>/dev/null || \
  echo "warning: BLOCKS missing FW/IWL8265.UCODE (iwl fw=miss)"
ls -lh "$BLOCKS_MNT/FW/WIFI.CFG" "$BLOCKS_MNT/FW/WIFI_H.CFG" "$BLOCKS_MNT/FW/WIFI_C.CFG" 2>/dev/null || \
  echo "note: BLOCKS missing FW/WIFI.CFG (iwl cfg=miss; cp WIFI_H or WIFI_C → WIFI.CFG)"
echo
echo "Boot Menu: select this USB (UEFI). Expect GOP desktop / Blocks ready."
echo "FW/WIFI*.CFG on the stick is preserved across sync (PSK stays on USB only)."
