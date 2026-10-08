# Hal/Common — 多架构共用的 HAL

放在这里的是 **仍属硬件抽象层**、但 **不绑死单一 CPU 架构** 的代码。  
操作系统合流逻辑在 `Kernel/Core/`，不要和本目录混为一谈。

| 文件 | 作用 |
| ---- | ---- |
| `HalCapability.c` | FB / ConsoleOnly / Virt 形态旗标；`HalCpuPark` |
| `HalVideoStub.c` | 非 X64 默认：只记视频配置、不画像素 |
| `Dtb.h` | 设备树（FDT）解析声明 |
| `RamFrameBuffer.h` | 内存帧缓冲登记 |

- 架构专有后端：`Hal/X64/`、`Hal/Arm64/`、`Hal/RiscV/`（如真 `HalVideo.c`）
- 板级地址宏：各 Arch 下的 `Board/<板名>/`
- X64 链真 `HalVideo`，不链 `HalVideoStub`；仍链 `HalCapability`
