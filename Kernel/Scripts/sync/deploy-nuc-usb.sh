#!/bin/bash
# deploy-nuc-usb.sh — X64 热更方案 1：UEFI 第一=U 盘；ksend 写该盘 Kernel.elf
#
# 认卷：BLOCKS.ID 内容 — U 盘=`USB`（sync-usb 戳），SSD=`SSD`（sync-nuc 戳）。
# 发送前对 A:…D: 逐个 `cat X:BLOCKS.ID`，命中 USB 再用 `X:Kernel.elf`。
#
# 前置：
#   1) NUC UEFI Boot Order #1 = 这张 Blocks U 盘
#   2) 已从该 U 盘进 Blocks；串口接 COM1 / USB-TTL
#   3) U 盘已 sync-usb 戳过 USB（旧盘请再 sync usb 一次）
#
# 用法：
#   deploy-nuc-usb --no-build --port /dev/ttyUSB0
#   deploy-nuc-usb --port /dev/ttyS0
#
# 判绿：KSEND OK → 复位 → 仍进 Blocks 且为新核。
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
BLOCKS_ROOT="$(cd "$HERE/../../.." && pwd)"
ELF="${BLOCKS_KERNEL_ELF:-$BLOCKS_ROOT/Kernel/Build/HAL/X64/Kernel.elf}"
PORT="${KSEND_PORT:-/dev/ttyUSB0}"
BAUD="${KSEND_BAUD:-115200}"
DO_BUILD=1
NO_REBOOT=0
DRY_RUN=0
EXTRA=()
ARGS=("$@")
I=0
while [ "$I" -lt "${#ARGS[@]}" ]; do
  A="${ARGS[$I]}"
  case "$A" in
    --no-build) DO_BUILD=0 ;;
    --no-reboot) NO_REBOOT=1 ;;
    --dry-run) DRY_RUN=1 ;;
    --port=*) PORT="${A#--port=}" ;;
    --port)
      I=$((I + 1))
      PORT="${ARGS[$I]:-}"
      if [ -z "$PORT" ]; then
        echo "error: --port needs a device" >&2
        exit 1
      fi
      ;;
    --elf=*) ELF="${A#--elf=}" ;;
    --elf)
      I=$((I + 1))
      ELF="${ARGS[$I]:-}"
      if [ -z "$ELF" ]; then
        echo "error: --elf needs a path" >&2
        exit 1
      fi
      ;;
    -h|--help)
      sed -n '2,18p' "$0" | sed 's/^# \?//'
      exit 0
      ;;
    *)
      echo "error: unknown arg: $A" >&2
      exit 1
      ;;
  esac
  I=$((I + 1))
done

if [ "$DO_BUILD" = 1 ]; then
  # shellcheck disable=SC1091
  . "$BLOCKS_ROOT/env.sh"
  ( cd "$BLOCKS_ROOT/Kernel" && make ARCH=x86_64 -j"$(nproc)" )
fi

if [ ! -f "$ELF" ]; then
  echo "error: missing $ELF" >&2
  exit 1
fi

echo "deploy-nuc-usb: ELF=$ELF"
echo "deploy-nuc-usb: port=$PORT — 先 cat ?:BLOCKS.ID 找 USB"

# 在板端壳上扫 A:–D: 的 BLOCKS.ID，正文含 USB（整行或子串）即 U 盘卷
TARGET="$(
  PORT="$PORT" BAUD="$BAUD" python3 - <<'PY'
import os, re, sys, time
try:
    import serial
except ImportError:
    print("error: need pyserial", file=sys.stderr)
    sys.exit(2)

port = os.environ["PORT"]
baud = int(os.environ.get("BAUD", "115200"))
ser = serial.Serial(port, baud, timeout=0.4)
ser.reset_input_buffer()

def roundtrip(cmd, wait=0.6):
    ser.reset_input_buffer()
    ser.write((cmd + "\r").encode("ascii", "replace"))
    time.sleep(wait)
    return ser.read(8192).decode("utf-8", "replace")

roundtrip("")  # wake
found = None
for letter in "ABCD":
    text = roundtrip("cat %s:BLOCKS.ID" % letter, 0.7)
    # 去壳回显噪声，认独立词 USB（勿误匹配）
    body = text.replace("\r", "")
    if re.search(r"(?m)^USB\s*$", body) or re.search(r"(?m)^\s*USB\s*$", body):
        found = letter
        print("deploy-nuc-usb: %s:BLOCKS.ID = USB" % letter, file=sys.stderr)
        break
    if "SSD" in body and "BLOCKS.ID" in body:
        print("deploy-nuc-usb: %s:BLOCKS.ID looks SSD (skip)" % letter, file=sys.stderr)
    elif "no such" in body.lower() or "FAIL" in body or "fail" in body:
        pass
    else:
        # 打印摘要便于手查
        snippet = " ".join(body.split())[:120]
        print("deploy-nuc-usb: %s:BLOCKS.ID ? %r" % (letter, snippet), file=sys.stderr)

ser.close()
if not found:
    print("error: 未找到 BLOCKS.ID=USB 的卷（先 sync usb 戳 U 盘；SSD 用 sync nuc 戳 SSD）",
          file=sys.stderr)
    sys.exit(1)
print("%s:Kernel.elf" % found)
PY
)"

echo "deploy-nuc-usb: target=$TARGET"

if [ "$DRY_RUN" = 1 ]; then
  EXTRA+=(--dry-run)
fi
if [ "$NO_REBOOT" = 1 ]; then
  EXTRA+=(--no-reboot)
fi

python3 "$BLOCKS_ROOT/Tools/Scripts/ksend.py" \
  --port "$PORT" \
  --baud "$BAUD" \
  --file "$ELF" \
  --path "$TARGET" \
  "${EXTRA[@]}"

echo "deploy-nuc-usb: 完。复位后须仍进 Blocks。"
