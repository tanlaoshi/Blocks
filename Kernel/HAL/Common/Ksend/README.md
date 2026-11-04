# Ksend — 增量写任意文件（Common，三架构）

不拔卡：PC 只发与卡上不同的 4 KB 块；写完可选复位。

- **实现**：本目录；门面 `Include/Hal/Ksend.h`
- **壳**：`Services/Shell/Commands/Ksend.c`（弱引用）
- **PC 日常**：`~/HotSend`（`hotsend` / `hotsend-gui`）
- **PC 金样/应急**：`Tools/Scripts/ksend.py`（已归档；协议对照）
- **传输**：串口（备份）或 TCP（网络热更主路，`Transport.c` / `Serve.c`）；有线/Wi‑Fi 同 listen
- **板差**：串口 `HalSerial*`；复位 `HalCpuReboot`（Duo RTC 见 `BoardReboot.c`）

```
# 日常（推荐）
hotsend --host 192.168.1.10 --file Kernel.elf

# 金样/应急
python3 Tools/Scripts/ksend.py --port /dev/ttyUSB0 --file Kernel.elf
python3 Tools/Scripts/ksend.py --host 192.168.1.10 --file Kernel.elf

deploy.sh --port /dev/ttyUSB0          # Duo 串口热更
deploy-nuc-usb --port /dev/ttyS0       # NUC 串口热更 U 盘
```

**NUC（方案 1）**：UEFI Boot Order #1 = Blocks U 盘；运行中须已从该盘启动；
`deploy-nuc-usb` / `ksend` 只写 U 盘 `BLOCKS:Kernel.elf`；复位后须仍进 Blocks
（进 Windows = Boot Order 未钉死）。不做 SSD/grub 链。

细节与差量表 `.crc` 约定见历史 Duo README / Docs/12 §部署闭环。
