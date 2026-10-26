# Hotres — x86 分辨率热切

| 文件 | 用途 |
| --- | --- |
| `Bochs.c` | QEMU/Bochs VBE DISPI（I/O `0x01CE`/`0x01D0`） |
| `Gop.c` | 真机：已是目标模式则同步 FB；**拒绝** EBS 后 live SetMode |

软件绘制仍在 [`HAL/Common/Video/`](../../../../Common/Video/)。门面：`HAL/X64/Hal/HalVideo.c` → `HalVideoCanHotSetMode` / `SetMode`。
