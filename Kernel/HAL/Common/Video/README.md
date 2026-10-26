# HAL/Common/Video — 软件帧缓冲绘制

Fill / Blit / Present / Glyph / 后缓冲。**arch 无关**；不含热切。

| 谁链 | 门面 |
| --- | --- |
| x86_64 | `HAL/X64/Hal/HalVideo*.c` |
| Arm64 / RiscV | `HAL/Common/HalVideo.c` |

| 不在本目录 | 落点 |
| --- | --- |
| Bochs / GOP 热切 | [`HAL/X64/Drivers/Video/Hotres/`](../../X64/Drivers/Video/Hotres/)（仅 x86 链） |
| 核显 Igpu | `HAL/X64/Drivers/Video/Igpu/` |
