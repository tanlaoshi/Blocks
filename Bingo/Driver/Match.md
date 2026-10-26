# Blocks 驱动匹配规范

> 操作页：[`驱动开发指南.md`](Guide.md) §5。  
> API：[`Kernel/Include/Driver/Driver.h`](../../Kernel/Include/Driver/Driver.h)。  
> 归档：[`过程/柱归档/设备管理器驱动匹配.md`](../../Archive/过程/柱归档/设备管理器驱动匹配.md)（若仍存）。

本文把现网 **三种 Probe 匹配模式**写清楚，并标明哪些是推荐、哪些**明确不做**。

---

## 0. 硬约束

| 项 | 结论 |
| --- | --- |
| 真 PCI 驱动应填表 | 新的 PCI 驱动抄 `Model/E1000/`：`DRIVER_MATCH` 写 VID/DID，或写 Class / Subclass / ProgIf（掩码 `0xFF`）。`Probe` 只用 `BusCtx` 上的那一个功能，不要再扫全总线 |
| 固定口与 USB 类仍可 `NULL` | `ps2-kbd`、`ata-pio`、virtio 自扫、`usb-msc` 门面、部分 wifi 仍是 `Match = 0`。它们不是认一块 PCI 功能 |
| `demo-driver` 单列 | 课堂靶，表匹配任意 Intel（`8086`，设备号通配），不按真实设备号 |
| 表驱动匹配 **已定型** | `DRIVER.Match` 类型为 `const DRIVER_MATCH *`；`Match != NULL` 时框架按表过滤未绑定设备，命中则 `Probe(Self, Dev, …)` 并绑定实例 |
| `Match` 的两种意思 | `NULL` = 驱动自扫；非 `NULL` = 表驱动 |
| 不改 API | `Probe` / `Bind` / `Remove` 三参数签名不变；`BusCtx` 对有表驱动为 `DEVICE_NODE *`，旧路径仍 `0` |
| `_template/` | 仍不编进 Kernel；模板默认 `Match = 0`，Demo 已填表 |

---

## 1. 现网怎么认设备

启动：各驱动经 `DRIVER_BOOT` → `DriverRegister` 只登记；随后 `DriverProbeClass(Class)`（或 `DriverProbeAll`）：

- `Match == NULL` → 旧路径 `Probe(Self, 0, &Private)`（驱动自扫 PCI/口/MMIO）；
- `Match != NULL` → 遍历未绑定 `DEVICE_NODE`，`DriverMatchesDevice` 命中则 `Probe(Self, (void *)Dev, &Private)`，成功则绑定（默认一驱动一实例；`MaxInstances` 可放宽）。

返回 0 再 `Bind`。`lsdev` 只列 **Bound** 实例。

有表时，框架先 `DriverMatchesDevice`，`Probe` 只处理命中的那一个 `DEVICE_NODE`。

本树 **没有** 完整 DTB `compatible` 匹配实现（枚举类型保留，x86 课路径不用）。

---

## 2. 三模式

新人拷 `_template/` 后，按设备类型选一种写在 `Probe` 里即可。三种可以叠（例如先查 VMM，再认 PCI）。

### 模式 A — 固定口 / MMIO

Probe 内直接碰约定地址；没有总线枚举。

| 代表 | 怎么认 |
| --- | --- |
| `ps2-kbd`（`Input/Ps2/`） | i8042 口 `0x60`/`0x64`；常要求 VMM 已开 |
| `ata-pio`（`Block/Ata/`） | 固定 I/O `0x1F0` |
| virt `virtio-*`（`HAL/Virtio/` / `Model/VirtioNet/`） | MMIO 窗扫描或 PCI 自认，**不是** DTB |

草稿：

```c
if (!VirtualMemoryEnabled()) return -1;
if (!Ps2InitHw()) return -1;   /* 或 Ata / VirtioMmioScan */
return 0;
```

### 模式 B — PCI 填表

表里写 class，或写 VID+DID。`Probe` 收到 `BusCtx`（`DEVICE_NODE *`）后再 map BAR。不要在 `Probe` 里扫全总线。

| 代表 | 表里写什么 |
| --- | --- |
| `e1000*` | VID `0x8086` + 已有 DID |
| `ahci` | class `01:06:01`，BAR5 |
| `nvme` | class `01:08:02`，BAR0 |
| `xhci` / `ehci` / `uhci` | USB class `0C:03`，ProgIF 分别是 `30` / `20` / `00` |
| `igpu` / `hda` | Intel `8086` + 显示类 `03`，或多媒体 `04:03` |

草稿：

```c
/* .Match = gE1000Match；Probe 不再扫总线 */
if (!Dev || Dev->Bus != DEVICE_BUS_PCI || Dev->Bar[0] == 0) return -1;
if (!E1000Setup()) return -1;
return 0;
```

### 模式 C — 软条件

Probe **不**证明「这块硬件已可用」，只满足策略（例如必须在 VMM 后、允许登记）。真正认盘/挂 Backend 放到 Bind 之后的显式步骤。

| 代表 | 怎么认 |
| --- | --- |
| `usb-msc`（`Msc/BlockMsc.c`） | Probe：策略通过则 `return 0`；真正挂盘常在后续 Claim / Shell 路径 |
| 课堂 `demo-driver` | Probe 恒 0；Bind 永不 `DriverInputAttach` |

草稿：

```c
if (!VirtualMemoryEnabled()) return -1;
return 0;   /* 上线策略另说，不要在 Probe 里死等热插拔 */
```

Demo 属于 C：为了 `lsdev` 可见，不是为了认真实硬件。

---

## 3. `DRIVER.Match` 表驱动

```c
typedef struct DRIVER {
    const char *Name;
    DRIVER_CLASS Class;
    const DRIVER_MATCH *Match;   /* NULL = 旧行为；非 NULL = 表驱动 */
    int (*Probe)(const struct DRIVER *Self, void *BusCtx, void **OutPrivate);
    int (*Bind)(DRIVER_INSTANCE *Inst);
    void (*Remove)(DRIVER_INSTANCE *Inst);
    int MaxInstances;
} DRIVER;
```

`DRIVER_MATCH`（`Include/Driver/Driver.h`）按 `Type` 分 PCI / DTB / FIXED，表以 `DRIVER_MATCH_NONE` 结尾；`DriverMatchesDevice`（`Core/Driver/`）逐条比对：

- **PCI**：`(Vendor & VendorMask)==(entry.Vendor & VendorMask)` 且 Device 同理；`Mask=0` 即通配。VID/DID 通过后，再比 `Class` / `Subclass` / `ProgIf`：对应掩码为 0 则忽略；掩码 `0xFF` 表示整字节相等。
- **DTB**：`Compatible` 串全等（实现保留；x86 课路径一般不用）；
- **FIXED**：`Bar[0]==entry.Base`。

`demo-driver`（`Drivers/DemoDriver.c`）：匹配任意 Intel PCI（`Vendor=0x8086` 精确、`Device` 通配），枚举序首张常为宿主桥，`Probe` 收到非空 `BusCtx`，`Bind` 占实例槽使 `lsdev` 可见。

旧的 `Match = 0` 只留给 §0 那张名单；**不要**把随机结构体指针塞进 `Match`——必须是 `DRIVER_MATCH[]`。

---

## 4. 明确不做

- 把固定口、USB 类设备做成 PCI `Match` 项（键盘不单独匹配）  
- 全体驱动改成可加载模块  
- 默认一驱动多设备（`MaxInstances` 已预留；课堂默认仍一实例）

---

## 5. 相关文档

| 文档 | 用途 |
| --- | --- |
| [`驱动开发指南.md`](Guide.md) | 5 步上手；Bind / NIC_L2 |
| [`驱动模板设计.md`](../../Archive/过程/柱归档/驱动模板设计.md) | 骨架与决策归档 |
| [`驱动框架现状分析.md`](../../Archive/过程/柱归档/驱动框架现状分析.md) | 现网驱动 Probe 对照（归档） |
| [`技术手册.md`](../../项目/技术手册.md) | 分层与 virtio / PCI 认法（若章节仍在） |
