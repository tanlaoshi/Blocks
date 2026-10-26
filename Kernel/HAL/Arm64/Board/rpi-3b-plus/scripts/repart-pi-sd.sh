#!/bin/bash
# repart-pi-sd.sh — 整盘 MBR：bootfs(FAT32 ~512M) + BLOCKS(FAT32 其余)，类型皆 0x0c
#
#   source ~/Blocks/env.sh && repart-pi-sd [/dev/sdX]
# 危险：擦掉整盘。对齐 X64：Kernel/Assets 只在 BLOCKS，bootfs 仅固件。
set -euo pipefail

DISK="${1:-}"
if [ -z "$DISK" ]; then
  # 读卡器上带 bootfs 或空盘
  for cand in /dev/sdc /dev/sdb /dev/sda; do
    [ -b "$cand" ] || continue
    if lsblk -ln -o NAME,LABEL "$cand" 2>/dev/null | grep -qE 'bootfs|BLOCKS'; then
      DISK=$cand
      break
    fi
  done
fi
if [ -z "$DISK" ] || [ ! -b "$DISK" ]; then
  echo "error: 指定块设备，如 repart-pi-sd /dev/sdc" >&2
  exit 1
fi
case "$DISK" in
  *[0-9]) echo "error: 要整盘（/dev/sdc）不要分区" >&2; exit 1 ;;
esac

echo "repart: 将擦除 $DISK"
lsblk "$DISK"
# 卸挂载
for p in "$DISK"*; do
  [ -b "$p" ] || continue
  udisksctl unmount -b "$p" 2>/dev/null || umount "$p" 2>/dev/null || true
done
sync

run_root() {
  if [ "$(id -u)" -eq 0 ]; then
    "$@"
  elif sudo -n true 2>/dev/null; then
    sudo "$@"
  else
    pkexec "$@"
  fi
}

run_root wipefs -a "$DISK" 2>/dev/null || true
# sfdisk 要 stdin；整段交给 root shell
run_root bash -c "sfdisk --wipe always '$DISK' <<'EOF'
label: dos
unit: sectors
,512MiB,0c,*
,,0c
EOF"
PART1="${DISK}1"
PART2="${DISK}2"
if [ ! -b "$PART1" ] && [ -b "${DISK}p1" ]; then
  PART1="${DISK}p1"
  PART2="${DISK}p2"
fi
sleep 1
run_root partprobe "$DISK" 2>/dev/null || true
sleep 1
run_root mkfs.vfat -F 32 -n bootfs "$PART1"
run_root mkfs.vfat -F 32 -n BLOCKS "$PART2"
sync
echo "repart OK: $PART1=bootfs $PART2=BLOCKS (FAT32 / type 0c)"
lsblk -o NAME,SIZE,FSTYPE,LABEL,PARTTYPE "$DISK"
