#!/bin/bash
# write-root-cfg.sh — 写 ESP 上 EFI/Blocks/ROOT.CFG（boot-root-cfg）
#
# 用法：
#   write-root-cfg.sh <EFI/Blocks 目录> <BLOCKS 挂载点>
#
# 写出：
#   disk=<PTUUID>   # 整盘 GPT DiskGUID
#   part=<PARTUUID> # BLOCKS 分区 GUID
#
# 取不到 GUID 则 warning 并返回 1（调用方勿当致命）。
set -euo pipefail

DEST_DIR="${1:-}"
BLOCKS_MNT="${2:-}"

if [[ -z "$DEST_DIR" || -z "$BLOCKS_MNT" ]]; then
  echo "usage: write-root-cfg.sh <EFI/Blocks-dir> <BLOCKS-mnt>" >&2
  exit 2
fi
if [[ ! -d "$BLOCKS_MNT" ]]; then
  echo "warning: write-root-cfg: BLOCKS mnt missing: $BLOCKS_MNT" >&2
  exit 1
fi

SRC="$(findmnt -n -o SOURCE --target "$BLOCKS_MNT" 2>/dev/null || true)"
if [[ -z "$SRC" ]]; then
  # 未挂进 findmnt 树时：调用方已保证可写目录，仍试 /dev 直认
  echo "warning: write-root-cfg: cannot resolve SOURCE for $BLOCKS_MNT" >&2
  exit 1
fi
# 去掉 partition mapper 后缀以外：用 lsblk 拿分区设备
PART_DEV="$(lsblk -npo NAME,TYPE "$SRC" 2>/dev/null | awk '$2=="part"{print $1; exit}')"
if [[ -z "$PART_DEV" ]]; then
  PART_DEV="$SRC"
fi
PARTUUID="$(lsblk -nro PARTUUID "$PART_DEV" 2>/dev/null | head -1 | tr '[:upper:]' '[:lower:]')"
# 盘 GUID：分区的 PKNAME → PTUUID
PK="$(lsblk -nro PKNAME "$PART_DEV" 2>/dev/null | head -1)"
if [[ -n "$PK" ]]; then
  PTUUID="$(lsblk -nro PTUUID "/dev/$PK" 2>/dev/null | head -1 | tr '[:upper:]' '[:lower:]')"
else
  PTUUID="$(lsblk -nro PTUUID "$PART_DEV" 2>/dev/null | head -1 | tr '[:upper:]' '[:lower:]')"
fi

if [[ -z "$PTUUID" || -z "$PARTUUID" || ${#PTUUID} -lt 36 || ${#PARTUUID} -lt 36 ]]; then
  echo "warning: write-root-cfg: no GPT GUIDs for $PART_DEV (PTUUID='$PTUUID' PARTUUID='$PARTUUID')" >&2
  exit 1
fi

if [[ -d "$DEST_DIR" && -w "$DEST_DIR" ]]; then
  mkdir -p "$DEST_DIR"
  {
    echo "# boot-root-cfg — written by sync; Boot picks OS volume"
    echo "disk=$PTUUID"
    echo "part=$PARTUUID"
  } > "$DEST_DIR/ROOT.CFG"
elif command -v sudo >/dev/null 2>&1; then
  sudo mkdir -p "$DEST_DIR"
  TMP="$(mktemp)"
  {
    echo "# boot-root-cfg — written by sync; Boot picks OS volume"
    echo "disk=$PTUUID"
    echo "part=$PARTUUID"
  } > "$TMP"
  sudo cp -f "$TMP" "$DEST_DIR/ROOT.CFG"
  rm -f "$TMP"
else
  echo "warning: write-root-cfg: cannot write $DEST_DIR" >&2
  exit 1
fi

sync
echo "ROOT.CFG -> $DEST_DIR/ROOT.CFG (disk=$PTUUID part=$PARTUUID)"
exit 0
