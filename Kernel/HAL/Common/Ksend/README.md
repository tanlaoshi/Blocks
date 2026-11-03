# Ksend — 增量写任意文件（Common，三架构）

不拔卡：PC 只发与卡上不同的 4 KB 块；写完可选复位。

- **实现**：本目录；门面 `Include/Hal/Ksend.h`
- **壳**：`Services/Shell/Commands/Ksend.c`（弱引用）
- **PC**：`Tools/ksend.py`
- **传输**：串口（备份）或 TCP（网络热更主路，`Transport.c` / `Serve.c`）；有线/Wi‑Fi 同 listen
- **板差**：串口 `HalSerial*`；复位 `HalCpuReboot`（Duo RTC 见 `BoardReboot.c`）

```
# 串口（备份）
python3 Tools/ksend.py --port /dev/ttyUSB0 --file Kernel.elf

# 网络主路：板上先有 IP（壳 `ip`），再 `ksend serve`；PC：
python3 Tools/ksend.py --host 192.168.1.10 --file Kernel.elf

deploy.sh --port /dev/ttyUSB0          # Duo 串口热更
deploy-nuc-usb --port /dev/ttyS0       # NUC 串口热更 U 盘
```

**NUC（方案 1）**：UEFI Boot Order #1 = Blocks U 盘；运行中须已从该盘启动；
`deploy-nuc-usb` / `ksend` 只写 U 盘 `BLOCKS:Kernel.elf`；复位后须仍进 Blocks
（进 Windows = Boot Order 未钉死）。不做 SSD/grub 链。

细节与差量表 `.crc` 约定见历史 Duo README / Docs/12 §部署闭环。
