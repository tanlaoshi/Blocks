#!/bin/bash
# 产出 HELLO.ELF / NETDEMO.ELF → Runtime/RootFs/X64/
set -eo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="${1:-$DIR/../../Runtime/RootFs/X64}"
mkdir -p "$ROOT"

gcc -nostdlib -ffreestanding -fno-pie -no-pie -static \
    -Wl,-T,"$DIR/hello.ld" -Wl,--build-id=none \
    -o "$ROOT/HELLO.ELF" "$DIR/Hello.S"
echo "User/X64 OK → $ROOT/HELLO.ELF"

gcc -nostdlib -ffreestanding -fno-pie -no-pie -static \
    -Wl,-T,"$DIR/hello.ld" -Wl,--build-id=none \
    -o "$ROOT/NETDEMO.ELF" "$DIR/NetDemo.S"
echo "User/X64 OK → $ROOT/NETDEMO.ELF"

ls -la "$ROOT/HELLO.ELF" "$ROOT/NETDEMO.ELF"
