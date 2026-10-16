# 驱动边界清理 — PR 拆分

> 状态：八刀已落地。`./build.sh` 与 `Runtime/smoke-boot.sh` 已过（2026-10-15）。
>
> 一设备一目录已经做过（`PR-MOD-drv-input` / `drv-block` / `drv-netglue`，见 [模块化与App课堂闭环.md](../过程/柱归档/模块化与App课堂闭环.md) §5）。本柱不重做搬家。
>
> 本柱要切的是依赖：驱动包含 `FileSystem.h`、`Scheduler.h`、`Console.h`，以及驱动之间互相包含对方的总头。

## 读完应有的样子

- 一块网卡、一块磁盘、一个键鼠控制器，可以只打开自己的目录读完。
- 驱动不包含 Services、Core 的头。需要读文件或让出 CPU 时，调用注册进来的函数。
- `Wifi` 不包含 `Ehci.h`，`XHCI` / `Ehci` 不包含 `Wifi.h`。U 盘不在 `UsbMsc.c` 里写两套 `Xhci*` / `Ehci*` 分支。声卡 HDMI 不直接写核显寄存器。
- `E1000`、`Ahci`、`Nvme`、`Ps2` 保持现在的样子，当作对照。

## 另立柱（不是永久不做）

这些写进「本柱不做」，是因为目标、风险和验收都跟「去掉错误 `#include`」不是一回事。另开文档或另开刀，不要塞进本八刀。

| 事项 | 为什么不进本柱 | 另去哪 |
| ---- | ---- | ---- |
| **Video / 画点 GUI 库** | 要从 `Services/Ui` 抽出可对外的几何库，接 `GfxSetPixel`；改的是 UI 与 Library，不是驱动依赖。本柱在 `PR-DRV-sched` 里只删 `VideoPresent.c` 对 `Scheduler.h` 的包含 | [独立GUI库-PR拆分.md](独立GUI库-PR拆分.md) |
| **Match 自动匹配** | 框架已能按表过滤；要迁的是真驱动的 Probe。行为面大，不塞进依赖清理 | [Match表驱动-PR拆分.md](Match表驱动-PR拆分.md) |
| **XHCI 键盘搬出目录** | §5.1 过渡期要求 HID 暂留控制器目录；未立总线口就搬家只是换文件夹 | [USB总线窄口-PR拆分.md](USB总线窄口-PR拆分.md)（先 UsbHostController，后可选搬 `UsbHid/`） |
| **Iwl 扫/关联/EAPOL** | 本柱只把算法进 `Crypto/`；协议仍在 Iwl 内 | 目录内分层：[Iwl目录Hw-Mac-PR拆分.md](Iwl目录Hw-Mac-PR拆分.md)。全核 `WifiMac` 另立，不在这两柱 |

## 本柱仍不做

- 不改 `DemoDriver.c` 的根上位置（课堂开关）。
- 每刀不改探测结果、不改寄存器偏移、不改盘上的固件路径字符串。

## 刀

顺序就是依赖顺序。前一刀没合，不开始后一刀。每刀 `cd Kernel && ./build.sh`。有行为的刀再跑 `Runtime/smoke-boot.sh`。

| 序 | PR | 改哪些文件 | 本刀不碰 | 验收 |
| --- | --- | --- | --- | --- |
| 1 | **PR-DRV-console** | `XHCI/Xhci.c` 删掉未使用的 `#include "Console.h"` | 枚举、HID、MSC | `build.sh`。`Xhci.c` 不再出现 `Console.h` |
| 2 | **PR-DRV-fw** | `Iwl/IwlCfg.c`、`Iwl/IwlFw.c`、`Wifi/WifiFw.c` 不再包含 `FileSystem.h`。读文件改成一个函数指针，开机时由已有文件系统模块注册。路径仍是现在的 `IWL_CFG_PATH`、`IWL_FW_PATH`、`WIFI_FW_PATH_*` | 配置解析、固件装载流程 | `build.sh`。三文件无 `FileSystem.h`。无卡、无固件时仍是软失败 |
| 3 | **PR-DRV-sched** | `Iwl/IwlHw.c` 的 `SchedulerIoBreath`，`Video/VideoPresent.c` 的 `SchedulerPreemptDisable` / `Enable`，改走窄钩子，去掉 `Scheduler.h` | 关抢占的那段区间、呼吸的调用次数 | `build.sh` + `smoke-boot.sh`。两文件无 `Scheduler.h` |
| 4 | **PR-DRV-wifi-claim** | `Wifi/Wifi.c` 不再包含 `Ehci.h`。`XHCI/XhciWifiClaim.c`、`Ehci/EhciWifiClaim.c` 不再包含 `Wifi.h`。认棒改成一张小函数表：控制器提供 Claim，Wifi 来调 | 8188 协议、PID 数值 | `build.sh`。上述三个文件互相不包含对方总头 |
| 5 | **PR-DRV-msc-host** | `Msc/UsbMsc.c` 里 `XhciMsc*` / `EhciMsc*` 的分支收成一张 `MSC_HOST` 表，两个控制器在就绪时注册。`UsbMsc.c` 不再包含 `XHCI.h`、`Ehci.h` | BOT、扇区读写、容量算法 | `build.sh`。QEMU 无 U 盘时启动与现在相同 |
| 6 | **PR-DRV-hda-pin** | `Hda/HdaHdmi.c` 不再包含 `Igpu.h`、不再调用 `IgpuMmio*`。核显侧留三个现有动作的包装：读、写、Forcewake。寄存器偏移仍在现在的调用处可见，只是换了个函数名进 Igpu | 偏移、使能位 | `build.sh`。真机 HDMI 音频本刀不作为必过 |
| 7 | **PR-DRV-iwl-crypto** | `IwlAes.c`、`IwlSha1.c`、`IwlSha256.c`、`IwlTkip.c`、`IwlTkipMic.c`、`IwlCcmp.c`、`IwlPbkdf.c` 移入 `Iwl/Crypto/`。Makefile 的 `Iwl/*.c` 通配要补上 `Iwl/Crypto/*.c` | 算法、EAPOL、扫描、关联 | `build.sh`。符号名不变 |
| 8 | **PR-DRV-serial** | `Drivers/Serial.c`、`Serial.h` 移入 `Drivers/Serial/`。模板 README 里「目标独立 Serial/」改成已完成 | `DemoDriver.c`、USB 转串口（FTDI/CDC 仍留在 `XHCI/`、`Ehci/`） | `build.sh`。根上不再有 `Serial.c` |

## 建议顺序

```text
PR-DRV-console
  → PR-DRV-fw
  → PR-DRV-sched
  → PR-DRV-wifi-claim
  → PR-DRV-msc-host
  → PR-DRV-hda-pin
  → PR-DRV-iwl-crypto
  → PR-DRV-serial
```

前三刀去掉向上的头文件。中间三刀去掉驱动互相点名。最后两刀只整理文件位置。

## 做完仍然大的两块

`XHCI/`（约 9,600 行）和 `Iwl/`（约 9,700 行）做完这些刀之后，行数不会少多少。`XHCI` 里的键盘、鼠标、U 盘传输、FTDI 仍在该目录。`Iwl` 的扫描、关联、EAPOL 仍在该目录。它们不再把 Services 和别的驱动总头拖进来，可以按子目录读：`XHCI` 看控制器与类，`Iwl` 看 `Crypto/` 与其余协议。

再往下拆类驱动或 EAPOL，单独立项，不塞进本柱。
