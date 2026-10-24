# HostChat

宿主机对端，给 Guest `CHAT.ELF` 测半双工。

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
