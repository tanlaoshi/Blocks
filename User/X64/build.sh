#!/bin/bash
# 产出 HELLO.ELF → Runtime/RootFs/X64/
set -eo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
OUT="${1:-$DIR/../../Runtime/RootFs/X64/HELLO.ELF}"
mkdir -p "$(dirname "$OUT")"
gcc -nostdlib -ffreestanding -fno-pie -no-pie -static \
    -Wl,-T,"$DIR/hello.ld" -Wl,--build-id=none \
    -o "$OUT" "$DIR/Hello.S"
echo "User/X64 OK → $OUT"
ls -la "$OUT"
