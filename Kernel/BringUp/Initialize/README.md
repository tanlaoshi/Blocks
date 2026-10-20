# BringUp/Initialize

`Modules.c` 阶段宏里的开机胶水（架构无关），跟模块表同属 BringUp，内部再调 `Hal*`。

| 文件 | 表名 | 所属阶段 |
| ---- | ---- | -------- |
| `Serial.c` | Serial | `MODULE_PHASE_EARLY` |
| `Video.c` | Video | `MODULE_PHASE_DISPLAY` |
| `Cpu.c` | Cpu | `MODULE_PHASE_PLATFORM`（阶段内最先） |
| `Usb.c` | USB | `MODULE_PHASE_INPUT`（仅 Full） |

同阶段其它：PMM/VMM/Driver/Scheduler→`Core/`；FS/Gui/Net/Console→`Services/`；Smp→各 arch `HalSmp*`。  
PLATFORM 序：Cpu→Driver→Smp；SESSION 在 FS/Gui/Net 前。见 `Modules.c` 头。  
驱动读文件 / 调度喘口气槽：[`../../HAL/Slots/`](../../HAL/Slots/)。
