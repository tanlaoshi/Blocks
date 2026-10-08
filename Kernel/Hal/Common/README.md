# Hal/Common — 多架构共用的 HAL 辅助

放在这里的是 **仍属硬件抽象层**、但 **不绑死单一 CPU 架构** 的代码。  
（操作系统合流逻辑在 `Kernel/Core/`，不要和本目录混为一谈。）

当前主要给 Arm64 / RiscV 的 QEMU virt 板共用：

| 文件 | 作用 |
| ---- | ---- |
| `Dtb.h` | 设备树（FDT）解析声明 |
| `RamFrameBuffer.h` | 内存帧缓冲登记 |

- 架构专有入口：`Hal/X64/`、`Hal/Arm64/`、`Hal/RiscV/`  
- 板级地址宏：各 Arch 下的 `Board/<板名>/`（如 `Board/virt/`）  
- X64 一般不使用本目录（UEFI GOP / 本机驱动）
