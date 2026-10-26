# Blocks 开机流程

> **目的**：冷启动链路 + 稳态四环 +「先骨后肉」读法；并标出**每步与硬件沟通的关键点**（错过就难以理解黑盒）。  
> **目录地图**：[`Tree.md`](Tree.md)。  
> **权威代码**：`Boot/BootPkg/`、`BringUp/`、`BringUp/Modules.c`、`Services/Tasks/`、`Core/Scheduler/Start.c`。  
> **活 ★**：[`路线图1023.md`](../../Documents/路线图1023.md)（日档）。已收日档 → [`项目/进展/`](../../Documents/项目/进展/README.md)。

---

## 0. 先骨后肉（阅读心智）

**骨架** = 加电后能调度、能泵输入、能挂一层 FS/网/屏的最小闭环。  
**肉** = 桌面装饰、商店、驱动超市、字库、lwIP、多板……按事件才进。

读代码：**先跟骨架走通一圈，再按故事往钩子上挂肉**；不要从整棵 `HAL/X64/Drivers` 或 `ThirdParty/lwip` 正面硬啃。

| 偏骨架 | 偏肉 |
| --- | --- |
| BringUp Entry/Handoff/Modules、Scheduler、PMM/VMM、Process/Elf/Syscall | Student 模板、演示槽 |
| Hal* 门面、当前板真正 Probe 到的几条 | 未接设备、他板、整棵 XHCI/网卡变体 |
| Tasks 四环、Shell 分发、Gui 开窗/合成主链 | Desktop 装饰、Store 全套、Apps UI |
| Fat / VFS 主路径 | 巨型字库、lwIP 细节 |

分层规则（谁能 include 谁）与「骨/肉」正交：见技术手册体系结构；Services 里既有骨（Tasks）也有肉（Store）。

### 0.1 读每个 `*Initialize` 只问三句

1. **跟硬件说话的那一行是什么？**（端口 I/O / MMIO / 固件表解析）  
2. **留下了什么全局契约？**（页表、设备节点、APIC 列表、`gConsole`…）  
3. **下一模块缺了它会怎样？**

代码量大时，优先找「神经接点」，不要先把整个驱动文件读完。

---

## 1. 冷启动总览

### 1.1 X64（课机 / QEMU / NUC）· 调用骨

```text
加电 → OVMF
  → Blocks Boot (BOOTX64.EFI)
       串口 → GOP → 读 Kernel.elf → ExitBootServices → jmp KernelEntry
  → BringUp/X64：
       KernelEntry          （换早期栈；空指针 hlt）
       KernelHandoff        （填 BOOT_HANDOFF + EnableEarlyIdentityMap）
  → KernelMain
       KernelAttachEarlyVideo
       ModulesRun(gModulesFull | Virt | VirtDesktop)
       KernelAfterModules
       KernelMainCommon     ← Create Shell / Gui / Worker [/ Input]
       SchedulerStart       ← 【模块表之后、进稳态的闸】
  → 稳态四环空转
       桌面：Gui 模块里已 GuiDesktopInitialize
       串口 “Blocks ready…”：Worker 末档宣告（不是 Init 最后一句）
```

**第一个函数（内核侧）**：`KernelEntry`（`BringUp/X64/KernelEntry.S`）。  
**进桌面后的闸**：`SchedulerStart`（`Core/Scheduler/Start.c`）——此前只是 Init，没有四环调度。

### 1.2 Arm64 / RiscV

**无 OVMF**。QEMU：`run-virt-arm` / `run-virt-riscv`（`-kernel` + DTB / OpenSBI）→ 同 `KernelMain`（模块表多为 virt / virt 桌面）。  
真机板（Pi3BPlus、Duo S）：U-Boot + DTB → 板包 UART；见板目录 README / 日档 rpi-desk。

Pi3BPlus 手测等价「关键点」：`go 0x80000` 后串口先见 Entry 探针 `*` / `+`（进了 `KernelEntry`），再谈横幅。

---

## 2. 三条贯穿全身的硬件通道

读任何驱动前先认路。PCI 枚举停在通道 1；真正驱动干活几乎都是 2 + 3。

| 通道 | CPU 怎么做 | 典型用途 |
| ---- | ---------- | -------- |
| **1. 端口 I/O** | `in` / `out`（`HalIoRead*` / `HalIoWrite*`） | COM1、**PCI 配置空间 CF8/CFC**、部分老设备 |
| **2. MMIO** | 页表映物理窗后 `*(volatile UINT32*)(base+off)` | xHCI、AHCI/NVMe、LAPIC、网卡 BAR、Pi mini-UART |
| **3. 中断** | 设备 → IOAPIC/MSI → IDT → ISR → 队列/调度 | 键鼠完成、块完成、网收包、定时器节拍 |

**Port ≠ 寄存器**：Port 是 `in`/`out` 的门牌（如 `0xCF8`）；真正的 PCI Vendor/BAR 等是配置空间里的寄存器，靠 Offset 区分，经 CF8 选址、CFC 读写。

---

## 3. Boot（固件侧 · X64）

| 步 | 做什么 | **关键点** |
| -- | ------ | ---------- |
| 串口 / GOP | 最早输出；选模式、清屏 | 仍在 Boot Services 世界 |
| 读核 | 找数据卷上的 `Kernel.elf` | 介质 + FAT/简单读 |
| 装载 | PT_LOAD；取入口 | 内核已在物理内存 |
| 提示 | ACPI RSDP、可选 xHCI MMIO | 塞进 `UEFI_BOOT_CONFIG` |
| 跳转 | **ExitBootServices** → `KernelEntry` | **此后不能再调 Boot Services** |

慢点候选：大分辨率 GOP；慢介质读 ELF。

---

## 4. Kernel 早期（模块表之前）

| 步 | 文件 | 做什么 | **关键点** |
| -- | ---- | ------ | ---------- |
| Entry | `BringUp/X64/KernelEntry.S` | 收 `UEFI_BOOT_CONFIG*`；换 `gEarlyStack` | 空指针直接 hlt（无串口字） |
| Handoff | `BringUp/X64/KernelHandoff.c` | 填 `BOOT_HANDOFF`；平台提示；内存图 | 见下 §4.1 |
| EarlyIdentity | `HAL/X64/PageTable/EarlyIdentity.c` | 静态池搭 4GiB 恒等并开 PG | **无 PMM 时也能摸低端 + 部分 MMIO** |
| KernelMain | `BringUp/Kernel.c` | 选 Full / virt / 串口子集 | `AttachEarlyVideo` 早挂串口/FB |
| ModulesRun | `BringUp/Modules.c` | 按表调 Init；进表前 `ConsolePublishAll` | 只接线，Console 开张在 SESSION |
| 建任务 + Start | `Scheduler/Start.c`、`Services/Tasks/` | 见 §7 | **Start 才真正开调度** |

串口 **`Blocks ready`**：Worker 末档宣告；桌面绘制在 Gui 模块的 `GuiDesktopInitialize`。

### 4.1 Handoff / 内存图要点

- 契约名：**`BOOT_HANDOFF`**（原 `BOOT_INFO`）；Common 只消费这份快照。  
- `Regions[]` 最多 **`BOOT_MEMORY_REGIONS_MAX`（64）条**——限制的是**段条数**，不是「一共多少 GB」。  
- 额外钉死的保留（`Free=0`，勿当空闲页）：  
  - `(0, 4096)`：第 0 页（空指针 / 历史 IVT）  
  - `(0x7000, 0x2000)`：SMP trampoline@`0x8000` + 临时栈@`0x7000`

---

## 5. `gModulesFull`：逐步关键点（桌面主路径）

硬脊：**PMM → VMM → Video → … → USB → FileSystem**（Usb 须在 FS 前）。  
SESSION 夹在 USB 与 FS 之间：调度钩先挂，再开命令表。

其它表：`gModulesVirt`（串口-only）、`gModulesVirtDesktop`（virt 桌面）。细节以 `Modules.c` 文件头为准。

### 5.1 总表（一页纸）

| 阶段 | 模块 | **关键点（黑盒接口）** | 留下的契约 | 错过会怎样理解错 |
| ---- | ---- | ---------------------- | ---------- | ---------------- |
| EARLY | Serial | COM1 **`inb`/`outb`**；`LibWrite` 挂串口写 | 最早可 Log | 不知字从哪出来 |
| EARLY | PhysicalMemory | handoff `Regions[]` → 段位图；`gPhysLock` | 可 `AllocPage` | 以为随便碰物理地址 |
| EARLY | VirtualMemory | 正式 PML4 + **`CR3`/开 PG**；FB/MMIO **恒等映** | 开 PG 后指针仍合法 | 不知为何 FB 要单独 Map |
| DISPLAY | Video | 写 **`FrameBufferBase`（VA=PA）**；PMM 后缓冲 | 能画 Logo / 后缓冲 | 把「清后缓冲」当「撕真屏」 |
| PLATFORM | Cpu | **`ArchInit`：GDT/IDT/LAPIC + sti**；syscall 门 | 能接中断 | 中断从天而降 |
| PLATFORM | Driver | 枚举：**PCI `outl 0xCF8` / `inl 0xCFC`**；只 Probe **Block** | 设备树 + 块后端 | 设备表从哪来；Input 误以为已起 |
| PLATFORM | Smp | **MADT → APIC ID 名单**；INIT/SIPI + trampoline | `gCpuCount` / AP Ready | 不知第二核怎么醒 |
| INPUT | USB | xHCI：**MMIO 门铃/环** + MSI；键鼠入队 | `HalInputPoll` 有货 | 以为 `read()` 键盘 |
| SESSION | Scheduler | 跑队列 + **`HalDriverSched`（IoBreath）**；**未 Start** | 驱动可喘气钩 | 以为已在切任务 |
| SESSION | Console | `gConsole.*` 函数表；命令 Register | Shell 可登记命令 | 和 UART 驱动混成一团 |
| Services | FileSystem | 块 MMIO/PIO → 扇区 → FAT/VFS | 路径可读文件 | 字符串直接变文件 |
| Services | Gui | 字体/主题（盘）→ **`GuiDesktopInitialize`** | 桌面已画 | 不知桌面何时出现 |
| Services | Network | 只建教学 UDP/TCP；**真网 Probe 在 Worker** | 协议壳 | 以为开机已 Claim 网卡 |
| （表后） | SchedulerStart | **开定时器 + 切入首任务** | 四环开始跑 | Init 完 = 已调度（错） |

### 5.2 EARLY — Serial

- **文件**：`BringUp/Initialize/Serial.c` → `HalSerial*` → x64 `Drivers/Serial`（16550）。  
- **关键**：端口读写；再开 RX IRQ；`LibWriteRegister(HalSerialWrite)`。  
- **薄封装**：BringUp 只认 `Hal*`；Arm/RiscV 可空实现。  
- **注意**：`Cpu` 里会再探一次 COM1（IDT 就绪后）。

### 5.3 EARLY — PhysicalMemory

- **文件**：`Core/PhysicalMemory/PhysicalMemory.c`、`Seg.c`。  
- **关键**：按 `BOOT_HANDOFF.Regions` 划段、建位图；挂 Bitmap（或 BestFit）政策；自旋锁保护 alloc/free。  
- **不做**：页表（下一格 VMM）。

### 5.4 EARLY — VirtualMemory

- **文件**：`Core/VirtualMemory/VirtualMemory.c`。  
- **关键顺序**：  
  1. `HalPageKernelSetup(IDENTITY_MB)` — 低端恒等（`IDENTITY_MB=4096` → 4GiB）  
  2. 按 handoff **补映 FB**（`MapIdentity`，VA=PA；Size 取 `FrameBufferSize` 与 pitch×高×4 较大者）  
  3. `HalPlatformMapMmio` — LAPIC / IOAPIC / XHCI fallback / runtime  
  4. `VirtualMemoryEnable` — 切到这张表  
- **为何 VMM 管 FB**：GOP 基址**不保证**落在低 4GiB 窗内；开 PG 后未映则写屏 #PF。  
- **开 PG 后映射保留**：PTE 已在核页表里，不会因 Enable 清掉。

### 5.5 DISPLAY — Video

- **文件**：`BringUp/Initialize/Video.c`。  
- **必须在 VMM 之后**（后缓冲要 PMM；LFB/igpu MMIO 要正式页表）。  
- **关键**：`HalVideoSet` + 后缓冲；**只清后缓冲**（Boot 已铺黑真屏，勿整屏 Present 撕边）；`DisplayHardwareInitialize`（igpu）；`DesktopBootLogoShow`。

### 5.6 PLATFORM — Cpu

- **文件**：`BringUp/Initialize/Cpu.c` → `HalInitialize` → x64 `ArchInit`。  
- **关键**：GDT/IDT/LAPIC、定时器 API（x64 上 `HalTimerInitialize` 可为**空壳**，开枪在后续 `HalTimerStart`）、syscall、FPU 岛自检（**拉 AP 前**）。  
- virt 串口路径可在此挂 virtio-input；真机键鼠留给 USB 模块。

### 5.7 PLATFORM — Driver（设备普查 + 只起块）

- **文件**：`Core/Driver/Driver.c`、`Core/Device/`、`HAL/X64/DeviceEnum/`、`HAL/X64/PCIe/PCIe.c`。  
- **枚举关键**（人口普查，不加载驱动）：  

```text
for Bus, Dev, Func:
  PciReadConfig(...)           ← 唯一雷达
    Address = enable|Bus|Dev|Func|Offset
    outl → 端口 0xCF8
    inl  ← 端口 0xCFC
  Vendor==0xFFFF → 空槽
  否则填 DEVICE_NODE（BAR/Class/IRQ 能力）→ DeviceAdd
再：按桥挂父子树；登记 MMIO BAR（仍只读）
```

- **登记**：`HalDriverRegister` 按链接段 Order 挂 `DRIVER` 表。  
- **Probe**：开机 **只** `DriverProbeClass(BLOCK)`。勿 `ProbeAll`（ps2 无 8042 会死转）。Input/Net 后置。

### 5.8 PLATFORM — Smp

- **文件**：`HAL/X64/SmpBoot/SmpBoot.c`、`HAL/X64/Acpi/Acpi.c`。  
- **名单关键**：`AcpiMadtParse(RSDP)` → 找签名 `APIC` 的 MADT → Type0/9 Local (x2)APIC 且 enabled → `gApicIds[]`。  
- **拉起关键**：对每个 AP `StartOneAp`（trampoline 物理 `0x8000`、INIT/SIPI）；AP 进 `SmpApEntry`，等 `SchedulerIsOnline` 再进 idle。  
- 失败软退单核；`gCpuCount` 只计实际起来的核。

### 5.9 INPUT — USB

- **文件**：`BringUp/Initialize/Usb.c` → `HalUsbInitialize`（xHCI 等）。  
- **关键**：控制器 MMIO + 事件/传输环 + 门铃；完成靠中断；软件侧 `HalInputPoll` / 键鼠队列。  
- 真机：Arm IRQ、鼠坐标交桌面、drain HID。无键鼠也继续（软成功）。

### 5.10 SESSION — Scheduler / Console

- **Scheduler**：清任务槽、挂政策、把 IoBreath 给驱动；**不开四环**（Create/Start 在 `KernelMainCommon`）。  
- **Console**：`PublishAll`（指针）→ 命令/alias → `Initialize`（挂 Gui ops / LibWrite）。  
  - `ModulesRun` 开头再 Publish 一次：供更早路径 `Register`/`Write` 指针非空。  
  - **接线 ≠ 开张**：整坨 `ConsoleInitialize` 不宜顶到表最前。

### 5.11 Services — FileSystem / Gui / Network

| 模块 | 关键点 | 备注 |
| ---- | ------ | ---- |
| FileSystem | 已 Probe 的块设备上读扇区；挂 FAT/RES；按策略 MSC claim | 网/iwl **不**在此 Claim |
| Gui | 盘上字体/主题/Db → Logo 收尾 → **`GuiDesktopInitialize`** | 软成功：缺资源也尽量进桌面 |
| Network | `UdpInitialize` / `TcpInitialize` | 真网卡：`boot-net-defer` → Worker |

---

## 6. 六个「错过就崩理解」的钉

第一次通读 Full 表，优先钉死这些（与上表对应）：

1. **CF8/CFC** — 谁在总线上  
2. **CR3/PG + FB 恒等** — 为何还能画屏  
3. **IDT/LAPIC** — 谁能打断 CPU  
4. **MADT + SIPI** — 第二核如何醒  
5. **xHCI 环/门铃** — 键鼠字节从哪来  
6. **`SchedulerStart`** — Init 结束 ≠ 已在调度  

---

## 7. 稳态四环 + 钩子

**Create 在 ModulesRun 返回之后**（SESSION 只登记，不开四环）。

```text
Input（够核时）── drain HID/XHCI
Gui ───────────── 唯一 GuiPollMouse；合成 / 栏
Shell ─────────── Present / 串口 / 键；blocks>
Worker ────────── MSC/钟、扫盘、Store、网后台…
```

### 7.1 只有一个 worker 吗？

**专用后台任务只有一个：`worker`（`WorkerTask`）**，钉在 **BSP CPU0**。

按优先级泵（忙则 `continue`，空则 `HalCpuHalt`）：

1. `StoreJobStep` — 商店装卸  
2. `HalIwlBgPump` — iwl 后台  
3. `LwIpDhcpStep`（真机 NIC 变化时可能先 Restart）

| 角色 | 任务 | 泵什么 |
| ---- | ---- | ------ |
| shell | `ShellTask` | 键鼠 dequeue、Present、串口、网轮询 |
| gui | `GuiTask` | `GuiPollMouse` / 桌面时钟 |
| input | `InputTask`（仅 **SMP≥3** → CPU2） | `HalInputPoll` |
| idle\* | 每核 | halt |

稳态 HID：SMP≥3 走 Input；否则 Gui/Shell 的 `YieldForPollInput`。  
**键鼠优先**：露光标后禁止饿死 HID；未露光标时勿狂泵鼠抢 xHCI（见仓库 `.cursor/rules`）。

### 7.2 钩子（骨架 → 肉）

| 钩子 | 骨架动作 | 挂上的肉（例） |
| --- | --- | --- |
| 鼠标 | `GuiPollMouse` → 点击 | 图标、菜单、开窗 |
| 开窗 | `GuiOpen*` | Settings / Files / Store / USER |
| 命令行 | `exec` / `run user` | Process/Elf、stdout 契约 |
| 热拔 | Worker → MSC | Mount、Fat、文件 App |
| 商店 | Worker Store* | Catalog / 装包 |
| 中断/驱动 | Hal 登记表 | 具体 NIC / 键鼠 / 块 |

推荐读法：

1. `BringUp/Kernel.c` → `Modules.c` → 本文 §5 → `Scheduler/Start.c` → Tasks 文件头  
2. Gui / Shell / Worker 各跟一圈空转  
3. 再按日档 ★ 挂肉；事故再钻某驱动一角  

第一次顺着读时，先跳过点菜单才进的 `*Ui`；驱动种类看 `Include/Driver/DriverBoot.h` 登记序。

---

## 8. 加速红线与慢点（备忘）

**勿为加速拆掉**：

- Usb **先于** FS  
- 无 HID / 无 NIC → **软退**，不挡桌面  
- Worker **不**在 Gui 里 Step 装卸（卡鼠标）  
- 无线 **永不串口打 PSK**  
- 键鼠优先（见仓库规则）

历史慢点（多已进 PR-BOOT-fast 归档）：Gui 墙纸/图标过早读盘、MSC 重试、iwl Claim 挡 FS、GOP 无谓 SetMode。  
测量：有 QEMU 时可用 `Kernel/Scripts/measure/` 一类脚本；真机以 NUC 墙钟为准。排期认**日档 ★**，大文件路线图只留史。

---

## 9. 骨架自测清单

能口头答上即可：

- [ ] Boot 如何交到 `KernelMain`；`BOOT_HANDOFF` 里有什么  
- [ ] PCI 枚举的两个端口是什么；MMIO 和端口 I/O 差在哪  
- [ ] 为何 VMM 要单独映 FB；VA 与 PA 在开机路径上是什么关系  
- [ ] MADT 提供什么；没有它能否拉 AP  
- [ ] 模块表里 Scheduler 与 `SchedulerStart` 谁先谁后、差在哪  
- [ ] 谁 Create 了哪几条任务、钉在哪核（含 Input 门槛）  
- [ ] Gui / Shell / Worker 稳态各泵什么；谁**不**泵鼠标  
- [ ] `exec` / 开窗如何挂上 Process  
- [ ] 块/键鼠是否只经 Hal 门面（业务直 `#include` 驱动私头 = 分层焊死）  

---

## 10. 修订

| 日期 | 说明 |
| ---- | ---- |
| 2026-10-23 | 合成：原「开机流程与加速」+「骨架与肉」；提示符 / 产品名对齐 Blocks。 |
| 2026-10-24 | 补「硬件沟通关键点」详版；`BOOT_INFO`→`BOOT_HANDOFF`；调用骨与 Full 表逐步钉点。 |
