# USB 总线窄口 — PR 拆分

> 状态：待看。还没改代码。
>
> 前置：[驱动边界清理-PR拆分.md](驱动边界清理-PR拆分.md) 已把 Wifi/MSC 收成主机表，去掉错误 `#include`。本柱更进一步：立一层 **USB 主机控制器公共口**，让 HID / MSC / 串口类代码不再 `#include "XHCI/XhciInternal.h"`。
>
> 对标 Linux：`xhci-hcd` / `ehci-hcd` + `usbcore` 管道；类驱动（`usbhid`、`usb-storage`）只走 URB，不碰 HC 私头。

## 与 §5.1 的关系

[模块化与App课堂闭环.md](../过程/柱归档/模块化与App课堂闭环.md) §5.1 现写：强绑定宿主的 HID **优先留在控制器目录**。那是过渡规矩（私头耦太死，先别硬搬）。

本柱完成后，把 §5.1 改成：

- **过渡**：仍强绑 `XhciInternal` 的文件可暂留控制器目录。
- **目标**：类驱动只依赖本柱的 `UsbHc`（名可微调）口；目录按类分（`UsbHid/`、`UsbMsc/`…），不按宿主分。

未立口之前，**禁止**只把 `XhciKeyboard*.c` 挪到平行 `Input/`——那只是换文件夹。

## 读完应有的样子

```text
Include/Driver/UsbHc.h     # 或 HAL/X64/Drivers/UsbHc.h：控制/中断/批量提交、认接口、断开
XHCI/                      # 只实现 UsbHc + 端口/环/枚举
Ehci/                      # 同上
UsbHid/ 或仍暂留 XHCI/     # 键盘鼠标：只 #include UsbHc.h
Msc/                       # 已有门面；BOT 走 UsbHc，不再点名 XhciMscBot 私符
```

类侧典型调用（示意）：

```c
int UsbHcControl(USB_PIPE *Pipe, USB_SETUP *Setup, void *Data, UINT32 Len);
int UsbHcIntrQueue(USB_PIPE *Pipe, void *Buf, UINT32 Len, void (*Done)(void *Ctx));
int UsbHcBulk(USB_PIPE *Pipe, void *Buf, UINT32 Len, int Out);
```

xHCI / EHCI 各自 `UsbHcRegister(...)`。上层不知道事件环、Slot、TD。

## 明确不做

- 不实现 Linux 级完整 `usbcore`（配置解析可仍在类驱动或现有 Enum 文件）。
- 不在本柱做热插拔策略大改；现有 Claim / HotPoll 语义保持。
- 不把 Iwl（PCIe 无线）扯进来。
- 不一开始就物理搬家；**先换调用边，后搬目录**（最后一刀可选）。
- 不改 PS/2、AHCI、E1000。

## 刀

每刀 `cd Kernel && ./build.sh`。键鼠/U 盘相关刀再跑 `Runtime/smoke-boot.sh`；有真机时加测 USB 键鼠。

| 序 | PR | 改哪些 | 本刀不碰 | 验收 |
| --- | --- | --- | --- | --- |
| 1 | **PR-USB-hc-contract** | 新增 `UsbHc.h`（管道/Setup/提交/认接口/Unregister）。空实现或仅声明 + 一份 `UsbHc.md` 对照现有 `Xhci*`/`Ehci*` 符号。更新本文件「读完应有的样子」若命名有微调 | 行为零变化 | `build.sh`。无链接新依赖导致的体积暴涨 |
| 2 | **PR-USB-hc-xhci** | xHCI 注册一张 `USB_HC_OPS`：把现有 Control / Intr / Bulk 路径封进 Ops（内部仍用现在的环与 Slot）。`UsbHc*` 转发到当前 HC | 不改 HID/MSC 源文件调用 | `build.sh` + smoke。键鼠路径未改调用前行为与搬前一致 |
| 3 | **PR-USB-hc-ehci** | EHCI 同样注册 Ops；无 xHCI 时 UsbHc 落到 EHCI（与现 Wifi/MSC 双宿主顺序一致：先 xHCI 再 EHCI） | HID 大搬 | `build.sh` |
| 4 | **PR-USB-msc-via-hc** | `Msc/` + 两边 BOT：对外读写只经 `UsbHcBulk` / Control；删除 `UsbMsc.c` 对宿主私符的残留（`MSC_HOST` 表可收成「谁提供 UsbHc」或保留 Claim 编排） | 扇区算法、容量 SCSI | `build.sh` + smoke。QEMU 无盘软失败与现在相同 |
| 5 | **PR-USB-hid-kbd** | 键盘路径（`XhciKeyboard*` / `InputXhci*` 中与报告相关的部分）改为 `UsbHcControl` + `UsbHcIntrQueue`；这些 `.c` **不再** `#include XhciInternal.h`（需要的常量进 `UsbHc.h` 或 `UsbHid.h`） | 鼠标、FTDI | `build.sh` + smoke。有键时 Shell 仍收键 |
| 6 | **PR-USB-hid-mouse** | 鼠标同键盘刀 | FTDI/CDC | `build.sh` + smoke。有鼠时光标可动（真机/QEMU 视现网能力） |
| 7 | **PR-USB-doc-515** | 改 §5.1 + `_template/README.md` + 驱动开发指南：目标按类分目录；过渡期允许暂留。本柱 README 链过去 | 代码 | 文档与实现一致 |
| 8 | **PR-USB-hid-move**（可选） | 键盘鼠标 `.c` 迁入 `Drivers/UsbHid/`（或 `Hid/`），Makefile 通配补一行。仅当 5–6 刀已无 `XhciInternal` 依赖 | 协议与报告格式 | `build.sh` + smoke。`XHCI/` 内无 Keyboard/Mouse 业务文件名 |

## 建议顺序

```text
PR-USB-hc-contract
  → PR-USB-hc-xhci
  → PR-USB-hc-ehci
  → PR-USB-msc-via-hc
  → PR-USB-hid-kbd
  → PR-USB-hid-mouse
  → PR-USB-doc-515
  →（可选）PR-USB-hid-move
```

先口、后类、最后才搬家。FTDI/CDC 可另开小刀，不堵本柱收官。

## 和驱动边界清理的分工

| 已做（边界清理） | 本柱 |
| --- | --- |
| `UsbWifiHost` / `MSC_HOST` 去掉总头互指 | 传输提交也走公共 Ops，不只 Claim 表 |
| 类代码仍可 `#include XhciInternal` | 类代码禁止 HC 私头 |
| §5.1 未改 | §5.1 改为过渡 + 目标 |

## 做完仍然在的

xHCI 事件环、Slot 上下文、hub 枚举仍在 `XHCI/`，体积仍大。变轻松的是：**读键盘时不必先读懂事件环布局**。
