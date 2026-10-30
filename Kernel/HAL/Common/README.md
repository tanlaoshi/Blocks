# HAL/Common — 跨 Arch 共享（规划外唯一额外顶栏）

HAL 顶栏只认：**X64 / Arm64 / RiscV / Virtio**，外加本目录。

| 子路径 | 内容 |
| --- | --- |
| `Video/` | 软件帧缓冲绘制（热切不在此；见 `X64/.../Hotres/`） |
| `HalVideo.c` | 非 x86 的 `HalVideo` 门面（Pi mailbox / virt ramfb；SetMode 恒失败） |
| `LwIp/` | lwIP 协议层端口（TcpSocket / TcpClient / TcpEcho / UdpQueue + `include/` + 非 x64 的 `LibcStub.c`）。**平台适配层的 `Netif.c` / `Ping.c` 仍留 `HAL/<Arch>/LwIp/`**——发送路径、MAC 来源、计时源各架构不同，是有意的混合形态 |
| `Slots/` | 驱动回调槽（FileRead / Sched） |
| `Board/` | 板包约定 README + `_template`（真板仍在 `HAL/<Arch>/Board/`） |

**Virtio/** 只放 virtio-mmio / blk / net / input / ramfb / DTB，不夹通用绘制。
