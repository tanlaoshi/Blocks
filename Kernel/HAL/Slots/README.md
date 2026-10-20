# HAL/Slots

驱动用的 **Hal 回调槽**（注册表），**不是** `Modules.c` 开机表里的一行。

| 文件 | 槽 | 谁 Register |
| ---- | --- | --- |
| `DriverFile.c` | `HalDriverFileRead` | `FileSystemInitialize` |
| `DriverSched.c` | IoBreath / Preempt* | `SchedulerInitialize` |

与 `BringUp/Initialize/` 分开：那边是 Serial/Cpu/Usb/Video 等**模块表胶水**；这边是横切钩子，避免驱动 `#include` FileSystem / Scheduler。
