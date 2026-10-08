# Hal/Arm64/Hal — 本架构私有的 HAL 细节

`Include/Hal/*.h` 是给 Common 看的**公共门面**（`HalSerialWrite`、`HalVideoSet`…）。

本目录放 **仅 Arm64 实现需要**、不宜泄漏给 Common 的类型与常量，例如：

| 文件 | 作用 |
| ---- | ---- |
| `HalPort.h` | 中断栈帧布局、向量号等；给本 Arch 的汇编/中断路径用 |

编译时 `-I Hal/Arm64/Hal`，实现文件可 `#include "HalPort.h"`。  
RiscV 对称目录：`Hal/RiscV/Hal/`。
