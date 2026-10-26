# Match 表驱动 — PR 拆分

> 状态：待看。还没改代码。
>
> 框架已有：`Match != NULL` 时 `ToyDriverProbeClass` 用 `DriverMatchesDevice` 过滤 `DEVICE_NODE`，命中则 `Probe(Self, Dev, …)`。课堂 `demo-driver` 已填一张 Intel PCI 表。规范：[驱动匹配规范.md](../手册/驱动/驱动匹配规范.md)。
>
> 本柱要做的是：**真驱动从「Probe 里自己扫 PCI」迁到填表**。不是重写匹配引擎。

## 读完应有的样子

- PCI 块设备、网卡、核显、声卡的 `TOY_DRIVER.Match` 指向 `DRIVER_MATCH[]`，以 `DRIVER_MATCH_NONE` 结尾。
- `Probe` 使用 `BusCtx`（`DEVICE_NODE *`）里的 Bus/Dev/Fn 和 BAR，不再自己 `PciScan*` 找第一张卡。
- 按 class 认的设备（AHCI、NVMe、xHCI、显示）能在表里写 Class/Subclass/ProgIf，不必伪造 VID。
- `Match == NULL` 仍合法：PS/2、ATA 固定口、USB 类（HID/MSC/8188）继续自扫，直到各自另柱。

## 明确不做

- 不改 `Probe` / `Bind` / `Remove` 的函数签名。
- 不一驱动多实例。两张同类网卡仍只绑枚举到的第一张（与现在「扫到第一张」同级）。要多实例另立柱。
- 不把 USB HID / MSC / 8188 的接口匹配塞进 `DRIVER_MATCH`。那些认的是 USB 接口，不是 PCI 功能。主机控制器（xHCI/EHCI/UHCI）可以按 PCI class 绑上；键盘仍在控制器 Probe 里枚举。USB 设备进设备树见 [USB总线窄口-PR拆分.md](USB总线窄口-PR拆分.md)。
- 不实现 DTB `compatible` 真枚举（类型在，树里基本没有 DTB 节点）。
- 不强制 PS/2、ATA 填表。设备枚举目前没有稳定的 `DEVICE_BUS_FIXED` 节点；固定口保持 `Match = 0`。
- 不改 `demo-driver` 的「任意 Intel、Bind 不抢输入」行为，除非 class 匹配改动波及它的通配（刀 1 验收必须仍能命中宿主桥）。

## 刀

每刀 `cd Kernel && ./build.sh`。迁真驱动的刀再跑 `Runtime/smoke-boot.sh`。有对应硬件的刀在 QEMU 或 NUC 上看 `lsdev` 名字与搬前一致。

| 序 | PR | 改哪些 | 本刀不碰 | 验收 |
| --- | --- | --- | --- | --- |
| 1 | **PR-MATCH-class** | `DRIVER_MATCH` 的 PCI 项增加可选 Class/Subclass/ProgIf 与掩码（`0xFF` = 忽略该字段，或单独 `MatchClass` 标志，刀内写死一种）。`DriverMatchesDevice` 在 VID/DID 通过后再比 class。`DEVICE_NODE` 已有这三项，枚举侧不用改。更新 [驱动匹配规范.md](../手册/驱动/驱动匹配规范.md) §3 | 不改任何真驱动的 `.Match` | `build.sh`。`demo-driver` 仍命中 `8086` 且 `lsdev` 可见 |
| 2 | **PR-MATCH-e1000** | 样板。`E1000/NetE1000.c` 填现有 DID 表（`E1000Probe.c` 的 `gE1000Ids`，Vendor `8086`）。`Probe` 只接受 `BusCtx` 上的那张卡并走现有 Setup。删掉「自己扫全总线找第一张」的路径 | 不改 PHY/TX、不加新 DID | `build.sh` + smoke。QEMU e1000：`lsdev` 仍有 e1000 名；无卡不挡启动 |
| 3 | **PR-MATCH-block** | `Ahci` 用 class `01:06:01`，`Nvme` 用 `01:08:02`（与 `AHCI_PCI_CLASS` / `NVME_PCI_CLASS` 一致）。Probe 吃 `BusCtx`。`Ata` **保持** `Match = 0` | 不改读写命令 | `build.sh` + smoke。QEMU 盘仍挂上 |
| 4 | **PR-MATCH-nic** | `Alx`、`Rtl`、`Iwl` 填各自已有 VID/DID。`Wifi`（USB 8188）**保持** `Match = 0` | 不改 L2 收发、不改 Iwl 固件路径 | `build.sh`。无这些卡时软失败与现在相同 |
| 5 | **PR-MATCH-disp-hda** | `Igpu` 用显示 class `03`（对上 `FindIntelDisplay`）。`Hda` 用它现有的 Intel HDA 查找条件写成表。Probe 吃 `BusCtx` | 不改 blit、不改 HDMI 引脚偏移 | `build.sh` + smoke。QEMU 无 Intel 核显时软退 |
| 6 | **PR-MATCH-usbhc** | `InputXhci` / EHCI / UHCI 的 **PCI 功能**按 USB class（`0C:03` + 对应 ProgIF）填表。`Probe(BusCtx)` 只初始化这一个控制器，再跑现有 HID/MSC 枚举。不把键盘做成独立 Match 项 | 不改报告格式、不改 hub 时序 | `build.sh` + smoke。QEMU 键鼠仍可用（现网 xHCI 路径） |
| 7 | **PR-MATCH-doc** | 规范 §0/§4：真 PCI 驱动「应填表」；固定口与 USB 类「仍可 NULL」。驱动开发指南 §4 补一行「新 PCI 驱动抄 E1000 的表，不要再写全总线扫描」 | 代码 | 文档与 `Match = 0` 的剩余名单一致（PS/2、ATA、usb-msc 门面、rtl8188eu、demo 除外或 demo 单列） |

## 建议顺序

```text
PR-MATCH-class
  → PR-MATCH-e1000
  → PR-MATCH-block
  → PR-MATCH-nic
  → PR-MATCH-disp-hda
  → PR-MATCH-usbhc
  → PR-MATCH-doc
```

先让 class 匹配可用，再用 E1000 钉死「表 + BusCtx」的写法，后面的刀照抄。usbhc 放后：它和 HID 枚举缠在一起，样板稳定后再动。

## 和别的柱

| 柱 | 关系 |
| --- | --- |
| [驱动边界清理](驱动边界清理-PR拆分.md) | 已做完。本柱不回头改那些钩子 |
| [USB总线窄口](USB总线窄口-PR拆分.md) | 正交。窄口解决「类驱动不包含 HC 私头」。本柱的 usbhc 刀只决定 **哪一个 PCI 功能** 交给 xHCI 驱动 |
| [Iwl目录Hw-Mac](Iwl目录Hw-Mac-PR拆分.md) | 正交。Iwl 填表只动 `NetIwl.c` 的 `.Match` 与 Probe 入口，不要求先拆目录 |

## 做完仍然 Match = 0 的

| 驱动 | 原因 |
| --- | --- |
| `ps2-kbd`、`ata-pio` | 固定口，设备树没有对应 `DEVICE_BUS_FIXED` 节点 |
| `usb-msc` 门面、`rtl8188eu` | 认 USB 设备，不是 PCI 功能 |
| `demo-driver` | 课堂靶，继续匹配任意 Intel |
