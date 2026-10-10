#!/bin/bash
# 产出 HELLO/NETDEMO/FORK/CAT/WRITE → Runtime/RootFs/X64/
set -eo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="${1:-$DIR/../../Runtime/RootFs/X64}"
mkdir -p "$ROOT"
CC_USER=(gcc -nostdlib -ffreestanding -fno-pie -no-pie -static
         -Wl,-T,"$DIR/hello.ld" -Wl,--build-id=none)

"${CC_USER[@]}" -o "$ROOT/HELLO.ELF" "$DIR/Hello.S"
echo "User/X64 OK → $ROOT/HELLO.ELF"

"${CC_USER[@]}" -o "$ROOT/NETDEMO.ELF" "$DIR/NetDemo.S"
echo "User/X64 OK → $ROOT/NETDEMO.ELF"

"${CC_USER[@]}" -o "$ROOT/FORK.ELF" "$DIR/Fork.S"
echo "User/X64 OK → $ROOT/FORK.ELF"

"${CC_USER[@]}" -o "$ROOT/CAT.ELF" "$DIR/Crt0.S" "$DIR/Cat.S"
echo "User/X64 OK → $ROOT/CAT.ELF"

"${CC_USER[@]}" -o "$ROOT/WRITE.ELF" "$DIR/Crt0.S" "$DIR/Write.S"
echo "User/X64 OK → $ROOT/WRITE.ELF"

ls -la "$ROOT/HELLO.ELF" "$ROOT/NETDEMO.ELF" "$ROOT/FORK.ELF" \
       "$ROOT/CAT.ELF" "$ROOT/WRITE.ELF"
