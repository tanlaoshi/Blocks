# Ksend — 串口增量写任意文件（Common，三架构）

不拔卡：PC 只发与卡上不同的 4 KB 块；写完可选复位。

- **实现**：本目录；门面 `Include/Hal/Ksend.h`
- **壳**：`Services/Shell/Commands/Ksend.c`（弱引用）
- **PC**：`Tools/ksend.py`
- **板差**：`HalSerialPutByte` / `HalMilliseconds` / `HalCpuReboot`（Duo 的 RTC 复位在 `milk-v-duo-s/BoardReboot.c`）

```
python3 Tools/ksend.py --port /dev/ttyUSB0 --file Kernel.elf
python3 Tools/ksend.py --port /dev/ttyUSB0 --file boot.sd --path BOOT:boot.sd --no-reboot --no-backup
deploy.sh --port /dev/ttyUSB0          # Duo：FIT → BOOT:boot.sd
deploy-nuc-usb --port /dev/ttyS0       # NUC 方案1：U 盘 BLOCKS:Kernel.elf
```

**NUC（方案 1）**：UEFI Boot Order #1 = Blocks U 盘；运行中须已从该盘启动；
`deploy-nuc-usb` / `ksend` 只写 U 盘 `BLOCKS:Kernel.elf`；复位后须仍进 Blocks
（进 Windows = Boot Order 未钉死）。不做 SSD/grub 链。

细节与差量表 `.crc` 约定见历史 Duo README / Docs/12 §部署闭环。
