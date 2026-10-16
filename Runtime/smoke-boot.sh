#!/bin/bash
#
# smoke-boot.sh — 无头冒烟（Blocks Boot + 现网能力核）
#
# 清残留 QEMU → 无头 run.sh → 串口关键字。
# 现网核日志为 ToyOS ready；兼容旧 Blocks ready。
#
# 用法：
#   cd ~/Blocks/Runtime && ./smoke-boot.sh
#   SMOKE_TIMEOUT=120 ./smoke-boot.sh
#   ./smoke-boot.sh --no-sync
#
set -eu
RUNTIME="$(cd "$(dirname "$0")" && pwd)"
cd "$RUNTIME"

TIMEOUT_SEC="${SMOKE_TIMEOUT:-90}"
LOG="${SMOKE_LOG:-/tmp/blocks-smoke-$$.log}"
NO_SYNC=0
for Arg in "$@"; do
    case "$Arg" in
        --no-sync) NO_SYNC=1 ;;
        -h|--help)
            sed -n '2,16p' "$0"
            exit 0
            ;;
        *)
            echo "unknown option: $Arg" >&2
            exit 1
            ;;
    esac
done

cleanup() {
    if [ -n "${QEMU_PID:-}" ] && kill -0 "$QEMU_PID" 2>/dev/null; then
        kill -9 "$QEMU_PID" 2>/dev/null || true
        wait "$QEMU_PID" 2>/dev/null || true
    fi
    ./run.sh --kill >/dev/null 2>&1 || true
}
trap cleanup EXIT

if [ ! -f RootFs/X64/Kernel.elf ]; then
    echo "error: missing RootFs/X64/Kernel.elf — build Kernel + ./sync.sh first" >&2
    exit 1
fi
if [ ! -f Esp/X64/EFI/BOOT/BOOTX64.EFI ]; then
    echo "error: missing Esp/X64/EFI/BOOT/BOOTX64.EFI — ./sync.sh first" >&2
    exit 1
fi

echo "smoke: timeout=${TIMEOUT_SEC}s log=${LOG} no_sync=${NO_SYNC}"
rm -f "$LOG"
: >"$LOG"

RUN_ARGS=(--headless)
if [ "$NO_SYNC" = 1 ]; then
    RUN_ARGS+=(--no-sync)
fi
./run.sh "${RUN_ARGS[@]}" >"$LOG" 2>&1 &
QEMU_PID=$!

log_text() {
    tr -d '\r' <"$LOG" 2>/dev/null || true
}

require() {
    local Pattern="$1"
    local Label="$2"
    if log_text | grep -E "$Pattern" >/dev/null 2>&1; then
        echo "smoke: PASS — $Label"
        log_text | grep -E "$Pattern" | tail -1 || true
        return 0
    fi
    echo "smoke: FAIL — missing: $Label ($Pattern)" >&2
    return 1
}

READY_RE='ToyOS ready|ToyOS 就绪|Blocks ready'
i=0
while [ "$i" -lt "$TIMEOUT_SEC" ]; do
    if log_text | grep -E "$READY_RE" >/dev/null 2>&1; then
        echo "smoke: PASS — ready"
        log_text | grep -E "$READY_RE" | tail -1 || true
        Fail=0
        # 现网桌面关键字较松：有 Gui 桌面或 Files/Shell 窗路径即可
        require 'Boot: Desktop Ready|Gui: desktop ready|Gui: desktop ok' 'desktop' || Fail=1
        if [ "${TOY_USB_MSC:-0}" = 1 ]; then
            require 'Boot: MSC Auto Mux OK' 'msc auto mux' || Fail=1
        fi
        if [ "$Fail" != 0 ]; then
            echo "smoke: FAIL — ready but desktop/shell checks failed" >&2
            log_text | tail -n 50 >&2 || true
            exit 1
        fi
        echo "smoke: PASS — all keywords"
        exit 0
    fi
    if ! kill -0 "$QEMU_PID" 2>/dev/null; then
        wait "$QEMU_PID" || true
        echo "smoke: FAIL — QEMU exited before ready" >&2
        log_text | tail -n 50 >&2 || true
        exit 1
    fi
    i=$((i + 1))
    sleep 1
done

echo "smoke: FAIL — timeout waiting for ready" >&2
log_text | tail -n 80 >&2 || true
exit 1
