# HostChat

宿主机对端，给 Guest `CHAT.ELF` 测半双工。  
**保留**：后续做 GUI 版宿主对端；现网仍可用本目录 `chatd` 或 `nc -l 9090`。

```bash
cc -O2 -o chatd chatd.c
./chatd          # :9090
# 或：nc -l 9090
```

Guest（QEMU SLIRP）：

```text
dbset chat.peer 10.0.2.2:9090
lwip on
exec Apps/chat/CHAT.ELF
```

`connected` / `chat>` 在 **GUI 窗客户区**，不是串口。
