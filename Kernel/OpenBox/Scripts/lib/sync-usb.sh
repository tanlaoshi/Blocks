#!/bin/bash
# sync-usb.sh — 把当前 ToyImage / 构建产物同步到已挂载的 ESP + BLOCKS 分区
#
# 真机布局（见 make-usb-stick.sh）：
#   ESP   → EFI/BOOT/BOOTX64.EFI
#   BLOCKS → RootFs/X64/（Kernel.elf、BLOCKS.ID、THEME、Assets、FW/、*.ELF…）
#
# 用法：
#   ./sync-usb.sh                 # 全量：RootFs/X64 → BLOCKS；ESP 已挂载才更新 BOOTX64.EFI
#   ./sync-usb.sh --build         # 先编 Kernel + Boot，再 prepare-rootfs，再全量同步
#   ./sync-usb.sh --kernel-only   # BLOCKS/Kernel.elf + FW/ + Assets/Icons/bmp48
#   ESP_MNT=/mnt/esp BLOCKS_MNT=/mnt/toy ./sync-usb.sh
#
# 兼容旧单分区：若只有 BLOCKS、没有 ESP，则把 EFI 也写进 BLOCKS（单 FAT 布局）。
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=toyos-common.sh
. "$SCRIPT_DIR/toyos-common.sh"
ROOT="$(toyos_image_root "$0")"
DO_BUILD=0
KERNEL_ONLY=0

# BLOCKS 卷上的无线固件目录（PR-N-wifi-1：FW/IWL8265.UCODE）
# WIFI.CFG / WIFI_H.CFG / WIFI_C.CFG 含 PSK：只活在 U 盘上；同步时保留、不覆盖、不 --delete。
sync_toyos_fw() {
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

find_label_dev() {
  local Label="$1"
  blkid -L "$Label" 2>/dev/null || true
}

find_label_mnt() {
  local Label="$1"
  local Dev Mnt
  Dev="$(find_label_dev "$Label")"
  if [[ -z "$Dev" ]]; then
    return 1
  fi
  Mnt="$(lsblk -ln -o MOUNTPOINT "$Dev" 2>/dev/null | awk 'NF{print; exit}')"
  if [[ -z "$Mnt" || "$Mnt" == "-" ]]; then
    return 1
  fi
  printf '%s\n' "$Mnt"
}

# 桌面常只挂 BLOCKS、不挂 ESP；UEFI 却从 ESP 启动 → 只改 BLOCKS/EFI 等于白改。
# 若 LABEL 存在但未挂载，挂到 /mnt/toyos-{esp,data}。
ensure_label_mounted() {
  local Label="$1"
  local DefaultMnt="$2"
  local Dev Mnt MUID MGID
  Dev="$(find_label_dev "$Label")"
  if [[ -z "$Dev" ]]; then
    return 1
  fi
  Mnt="$(lsblk -ln -o MOUNTPOINT "$Dev" 2>/dev/null | awk 'NF{print; exit}')"
  if [[ -n "$Mnt" && "$Mnt" != "-" ]]; then
    printf '%s\n' "$Mnt"
    return 0
  fi
  MUID="$(id -u)"
  MGID="$(id -g)"
  mkdir -p "$DefaultMnt"
  if mount -o "uid=${MUID},gid=${MGID},umask=022" "$Dev" "$DefaultMnt" 2>/dev/null; then
    echo "mounted LABEL=$Label $Dev -> $DefaultMnt" >&2
    printf '%s\n' "$DefaultMnt"
    return 0
  fi
  if command -v sudo >/dev/null 2>&1 && \
     sudo mount -o "uid=${MUID},gid=${MGID},umask=022" "$Dev" "$DefaultMnt" 2>/dev/null; then
    echo "mounted LABEL=$Label $Dev -> $DefaultMnt (sudo)" >&2
    printf '%s\n' "$DefaultMnt"
    return 0
  fi
  echo "error: found LABEL=$Label at $Dev but cannot mount (need root for ESP)." >&2
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
if [[ -z "$BLOCKS_MNT" ]]; then
  BLOCKS_MNT="$(find_label_mnt TOYOS || true)"
fi

# 常见自动挂载路径兜底
if [[ -z "$BLOCKS_MNT" ]]; then
  for d in /media/*/BLOCKS /media/"${USER:-tank}"/BLOCKS /run/media/"${USER:-tank}"/BLOCKS \
           /media/*/TOYOS /media/"${USER:-tank}"/TOYOS /run/media/"${USER:-tank}"/TOYOS; do
    if [[ -d "$d" ]]; then BLOCKS_MNT="$d"; break; fi
  done
fi
if [[ -z "$ESP_MNT" ]]; then
  for d in /media/*/ESP /media/"${USER:-tank}"/ESP /run/media/"${USER:-tank}"/ESP \
           /media/*/EFI /media/"${USER:-tank}"/EFI; do
    if [[ -d "$d" ]]; then ESP_MNT="$d"; break; fi
  done
fi

# 未挂载则尝试挂上（尤其 ESP：否则 UEFI 仍跑旧 BOOTX64）
if [[ -z "$ESP_MNT" ]]; then
  ESP_MNT="$(ensure_label_mounted ESP /mnt/toyos-esp || true)"
fi
if [[ -z "$ESP_MNT" ]]; then
  ESP_MNT="$(ensure_label_mounted EFI /mnt/toyos-esp || true)"
fi
if [[ -z "$BLOCKS_MNT" ]]; then
  BLOCKS_MNT="$(ensure_label_mounted BLOCKS /mnt/toyos-data || true)"
fi
if [[ -z "$BLOCKS_MNT" ]]; then
  BLOCKS_MNT="$(ensure_label_mounted TOYOS /mnt/toyos-data || true)"
fi

if [[ -z "$BLOCKS_MNT" ]]; then
  echo "error: BLOCKS volume not mounted (label BLOCKS)." >&2
  echo "  Format: $ROOT/make-usb-stick.sh --yes --sync" >&2
  echo "  Or mount the BLOCKS partition and re-run." >&2
  exit 1
fi

# --kernel-only：只碰 BLOCKS，不要求 ESP（避免误判 single-FAT / 往 ESP 写 Kernel）
if [[ "$KERNEL_ONLY" -eq 1 ]]; then
  if [[ "$DO_BUILD" -eq 1 ]]; then
    echo "== build Kernel =="
    (cd "$ROOT/../ToyKernel" && ./build.sh)
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
    "$(toyos_kernel_build_dir "$(toyos_resolve_root "$0")")/HAL/X64/Kernel.elf" \
    "$ROOT/RootFs/X64/Kernel.elf"
  do
    if [[ -f "$C" ]]; then SRC="$C"; break; fi
  done
  if [[ -z "$SRC" ]]; then
    echo "error: no Kernel.elf — build first or drop --kernel-only" >&2
    exit 1
  fi
  if [[ -d "$BLOCKS_MNT/EFI/BOOT" && ! -f "$BLOCKS_MNT/BLOCKS.ID" && ! -f "$BLOCKS_MNT/TOYOS.ID" ]]; then
    echo "error: $BLOCKS_MNT looks like ESP (EFI/BOOT present, no BLOCKS.ID)." >&2
    echo "  Use the data partition (LABEL=BLOCKS), e.g. /media/\$USER/BLOCKS" >&2
    exit 1
  fi
  cp -f "$SRC" "$BLOCKS_MNT/Kernel.elf"
  sync_toyos_fw "$BLOCKS_MNT"
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
# 桌面常把 ESP 挂在 /mnt/toyos-esp，但 blkid 无 LABEL=ESP
if [[ -z "$ESP_MNT" && -d /mnt/toyos-esp/EFI/BOOT ]]; then
  ESP_MNT=/mnt/toyos-esp
  echo "note: using /mnt/toyos-esp as ESP (no LABEL=ESP in blkid)"
fi
if [[ -z "$ESP_MNT" ]]; then
  if [[ -n "$ESP_DEV" ]]; then
    echo "warning: USB ESP/EFI ($ESP_DEV) is not mounted — bootloader unchanged." >&2
    echo "  BLOCKS still gets a full RootFs sync." >&2
    echo "  To update BOOTX64.EFI: sudo mount $ESP_DEV /mnt/toyos-esp && ESP_MNT=/mnt/toyos-esp $0" >&2
    SKIP_ESP=1
  elif lsblk -n -o NAME,MOUNTPOINT 2>/dev/null | grep -q toyos-esp; then
    echo "warning: dual-partition USB but ESP mount not resolved — bootloader unchanged." >&2
    echo "  BLOCKS still gets a full RootFs sync. Or: ESP_MNT=/mnt/toyos-esp $0" >&2
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
  (cd "$ROOT/../ToyKernel" && ./build.sh)
  echo "== build Boot =="
  if [[ -x "$ROOT/../ToyBoot/build.sh" ]]; then
    (cd "$ROOT/../ToyBoot" && ./build.sh)
  else
    echo "warning: ToyBoot/build.sh missing; keep existing BOOTX64.EFI" >&2
  fi
fi

echo "== prepare-rootfs =="
"$SCRIPT_DIR/prepare-rootfs.sh"

BOOT_EFI="$ROOT/Esp/X64/EFI/BOOT/BOOTX64.EFI"
if [[ "$SKIP_ESP" -eq 0 && ! -f "$BOOT_EFI" ]]; then
  echo "error: missing $BOOT_EFI — build ToyBoot" >&2
  exit 1
fi

if [[ "$SKIP_ESP" -eq 0 && "$SINGLE_FAT" -eq 0 ]]; then
  echo "== sync ESP (Boot) =="
  mkdir -p "$ESP_MNT/EFI/BOOT"
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
sync_toyos_fw "$BLOCKS_MNT"

if [[ "$SINGLE_FAT" -eq 1 ]]; then
  # 仅单分区盘：启动与系统同卷，EFI 只能放 BLOCKS
  mkdir -p "$BLOCKS_MNT/EFI/BOOT"
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
