# HAL/Common/Video — 软件帧缓冲绘制

Fill / Blit / Present / Glyph / 后缓冲。arch 无关。

| 谁链 | 门面 |
| --- | --- |
| x86_64 | `HAL/X64/Hal/HalVideo*.c` |
| Arm64 / RiscV | `HAL/Common/HalVideo.c` |

核显 Igpu 仍在 `HAL/X64/Drivers/Video/Igpu/`。
