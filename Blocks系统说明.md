# Blocks 系统说明（权威文档）

> **权威地位**：迁移过程中的**标杆文档**。能力对错以现网已跑通源码为准；**设计怎么讲、依赖怎么画、逻辑怎么走**——以本文 + 源码「谁调用」注释为准。冲突时：先改代码或先改本文，二者必须对齐后再 TG。  
> **必须写清四件事**（每个机制/子系统小节同一套栏）：  
> 1. **职责**（干什么 / 不干什么）  
> 2. **设计原理**（为何这样拆、对标什么）  
> 3. **运行逻辑**（开机或运行时按什么顺序发生）  
> 4. **依赖关系**（依赖谁、被谁依赖、禁止的反向依赖）  
> **写法**：已落地的写满四栏；未齐标 **待补**；JX 动到某块时**随时**补本文。  
> **配套**：[`调用链.md`](调用链.md)（逐步函数名）· 源码块注释（函数级）· [`积木原则.md`](积木原则.md)（硬约束）· [`Kernel迁移.md`](Kernel迁移.md)（进度/差缺）。  
> **工作区**：`~/Blocks` · **对照源**：`~/tanlaoshi/edk2/ToyKernel`（只读）。

---

## 0. 文档职责与维护

### 0.1 三层读法

| 层 | 载体 | 粒度 |
| -- | ---- | ---- |
| **权威总览** | **本文** | 职责 / 原理 / 运行逻辑 / 依赖 |
| **主路径步骤** | [`调用链.md`](调用链.md) | 函数名 + 文件 + 顺序 |
| **函数级** | 各 `.c` 块注释 | 做什么 / 谁调用 / 前后文 |

### 0.2 子系统写作模板（强制）

迁入或实质改某子系统时，在 §5 增加或更新一节，**四栏不可删**：

```markdown
### 5.x 名称（状态：已落地 | 薄 | 待补）

**职责**：…
**设计原理**：…
**运行逻辑**：
1. …
**依赖关系**：
- 依赖：…
- 被依赖：…
- 禁止：…
**入口与源码**：`路径` · 关键符号…
**与现网**：对齐何处 / 刻意改何处
```

### 0.3 迁移标杆（Agent / 人）

| 检查 | 要求 |
| ---- | ---- |
| 只改代码不改本文 | **不合格**（JX 动子系统必须补四栏） |
| 只写「做什么」无原理/依赖 | **不合格** |
| 主链变了不改调用链 | **不合格** |
| 函数无「谁调用」 | **不合格**（见注释规范） |

---

## 1. Blocks 是什么

**职责**：教学向小型 OS——UEFI 加载自研内核，提供桌面、串口 Shell、FAT 卷、基础网络与用户态 ELF。  

**设计原理**：相对现网 ToyKernel，能力对齐课堂主路径（路线图 §1），同时用分层、注册表、目录命名空间、强制注释，把「当年为跑通快堆、没时间吃透」的设计在迁移中讲清楚、改干净。  

**运行逻辑（整机）**：见 §2。  

**依赖关系（整机）**：

```text
硬件 / 固件(OVMF)
    ↑ Boot（UEFI 应用）加载
Kernel.Hal（摸硬件）
    ↑ Core 调用 Hal* 契约
Kernel.Core（政策与编排）
    ↑ syscall / 加载
User ELF
```

Runtime（QEMU 盘镜像、脚本）在仓库外支撑验收，不链进 Kernel。

---

## 2. 上电到可用（X64）

**职责**：从加电到「桌面 + Shell 可输入」的主路径。  

**设计原理**：Boot 只负责加载与 handoff；内核用**模块表**拼开机，避免 `KernelMain` 堆政策；之后 `ConsoleRun` 兼泵 Gui 与 Shell。  

**运行逻辑**：

```text
加电 → OVMF → BOOTX64.EFI（UefiMain）
  → 串口 / GOP / 读 Kernel.elf + BLOCKS 线索 / ExitBootServices
  → KernelEntry → KernelHandoff → KernelMain
       → BootInfoSave → EarlyIdentity → 早期串口/视频
       → ModulesRunFull（§3.1 表序）
       → ConsoleRun：Hello（可选）+ 循环 GuiPoll / 读行 / Shell 分发
```

逐步符号见 [`调用链.md`](调用链.md)。  

**依赖关系**：Boot 依赖固件协议与盘上 ELF；Kernel 依赖 Boot 留下的 `BOOT_INFO`；Console 依赖 Gui/FS/Net 等模块已 Initialize。  
**禁止**：Kernel 再回调 UEFI Boot Services（ExitBS 之后）。

---

## 3. 核心机制（必懂）

### 3.1 开机模块表

**职责**：按固定顺序启动各 Core 子系统；失败则停并打 `[Mod]` 日志。不实现业务。  

**设计原理**：表驱动 / 拼表开机（积木）。每项 `{ 名字, Initialize 函数指针 }`。换实现 = 换某模块 `.c` 或改表项，不把 if 政策堆进 `KernelMain`。对标常见嵌入式/教学 OS 的 initcall 表思路（实现更薄）。  

**运行逻辑**：

1. `KernelMain` 调用 `ModulesRunFull`  
2. `ModulesRun` 遍历 `gModulesFull[]`  
3. 每项：日志 → 调 `Init()` → 非 0 则 Failed 返回 -1  

**当前表序与为何大致如此**：

| 序 | 模块 | 设计理由（依赖） |
| -- | ---- | ---------------- |
| 1 | Serial | 后续一切要能打日志 |
| 2 | Memory | 分配页的前提 |
| 3 | VirtualMemory | 依赖 PMM；为 MMIO/用户页表铺路 |
| 4 | Driver | 设备壳；枚举可挂 Hal |
| 5 | Video | 桌面与字需要 FB |
| 6 | Cpu | 中断/段等，供后模与用户态 |
| 7 | USB | 输入等；需 VMM 映 BAR（高址） |
| 8 | FileSystem | 主题/库/店/ELF 在盘上 |
| 9 | Network | 相对独立；可课演示 |
| 10 | Gui | 依赖 Video/FS/输入路径 |
| 11 | Scheduler | 定时与让出；泵循环要用 |
| 12 | Console | 最后：注册 Shell，进入人机循环 |

**依赖关系**：

- 依赖：`KernelMain` 已完成 EarlyIdentity / BootInfo  
- 被依赖：所有表后逻辑（Console/Gui 运行时）  
- 禁止：在 `Modules.c` 写 FAT/窗/协议细节  

**入口与源码**：`Core/Modules.c` · `ModulesRunFull` / `gModulesFull`。

---

### 3.2 驱动分层（HAL / Core）

**职责**：Hal 独占硬件细节；Core 做策略与用户可见行为。  

**设计原理**：单向依赖，避免「驱动 include 内存/Gui 政策」的现网债。设备实现进 `Hal/<Arch>/Drivers/<Device>/`（一设备一夹）。公开契约在 `Include/Hal/*.h`。  

**运行逻辑**：

1. 模块表调到 USB/Network/FileSystem 等  
2. Core `*Initialize` 调 `Hal*Initialize` / 枚举  
3. 运行时 Core 只经 Hal 门面读写设备  

**依赖关系**：

```text
Core ──调用──→ Hal（Include/Hal）
Hal ──禁止──→ Core 政策头（PhysicalMemory.h / Gui.h / …）
```

需要反向能力时：Core **注入** Ops/回调，或能力下沉为 Hal 自持——不直 include。  

**入口与源码**：[`积木原则.md`](积木原则.md) §2 · 例：`Hal/X64/Drivers/UsbHid/`。  
**待补**：Virtio/PS2/xHCI 仍在 Hal 根扁平（PR-B-hal）。

---

### 3.3 注册制

**职责**：把「可扩展入口」从巨石文件里拆成报名表，运行时按表分发。  

**设计原理**：开闭原则的薄实现——加命令/加模块 = 新编译单元 + Register，不改中央 switch 长龙。  

**运行逻辑（Shell）**：

1. `ConsoleInitialize` → `ShellCommandInitialize`  
2. 核心命令 Register + `FileSystemRegister` / `ThemeRegister` / `Network*Register` / …  
3. `ConsoleRun` 读行 → `ShellCommandRunLine` → 查表调用  

**运行逻辑（模块表）**：见 §3.1（报名的是 Initialize，不是命令字）。  

**依赖关系**：

- Shell 命令实现可依赖 FS/Net/Gui API；**注册表本身**只存函数指针  
- 禁止：命令实现反向改 Modules 表；禁止 Hal 注册 Core Shell 命令  

**入口与源码**：`Core/Console/ShellCommand/` · `ShellCommandRegister`。

---

## 4. 命名与注释标杆

**职责**：保证可读、可讲、可迁。  

**设计原理**：目录即命名空间；全词拼写；注释补调用图。  

**运行逻辑**：改代码 → 当场自查命名/行数/谁调用 → 补本文四栏 → 再报 JX。  

**依赖关系**：规范文 [`命名规范盘点-Blocks.md`](命名规范盘点-Blocks.md)、[`代码注释规范.md`](代码注释规范.md)；不替代本文的原理/依赖栏。

---

## 5. 子系统（权威分册 · 四栏）

### 5.1 Boot（已收口）

**职责**：在 UEFI 下把 `Kernel.elf` 装入内存，收集 GOP/盘/RSDP/xHCI 等线索，ExitBootServices 后跳转内核；之后不再当「OS」。  

**设计原理**：加载器与内核分离；handoff 用扁平结构（`UEFI_BOOT_CONFIG` → `BOOT_INFO`），避免内核依赖 UEFI 协议栈。  

**运行逻辑**：

1. `UefiMain` 编排  
2. 串口、视频模式  
3. `BootLoadKernel`（BLOCKS 卷优先）  
4. 填 RSDP/xHCI 等  
5. `JumpToKernel`  

**依赖关系**：

- 依赖：OVMF、GPT/FAT 上的 Kernel.elf、GOP  
- 被依赖：全内核（唯 handoff）  
- 禁止：把文件系统政策写进 Boot（只加载）  

**入口与源码**：`Boot/BootPkg/Boot.c` · [`Boot迁移.md`](Boot迁移.md)。  
**与现网**：接棒语义对齐；路径/命名以 Blocks 为准。

---

### 5.2 内存与虚拟内存（已进表）

**职责**：物理页分配（PMM）；内核页表与 MMIO 映射；可选 MEMORY_OPS 换实现。不做文件系统。  

**设计原理**：先恒等映射保证早期与高址 MMIO 可访问；PMM 与 VMM 分模块，表序上 Memory → VirtualMemory。Ops 门面便于换算法而不改调用方。  

**运行逻辑**：

1. `EarlyIdentitySetup/Enable`（入核后、模块表前）  
2. `MemoryInitialize` → `PhysicalMemoryInitialize` + 自检  
3. `VirtualMemoryInitialize`；USB 等按需 `MapMmio`  

**依赖关系**：

- 依赖：BootInfo 内存图；Hal 仅早期页表装配  
- 被依赖：几乎所有需动态页/映射的模块  
- 禁止：Hal 驱动直接 `#include` PhysicalMemory 政策（应经注入或 Core）  

**入口与源码**：`Core/Memory/`、`Core/VirtualMemory/`。  
**与现网**：骨架对齐；细策略 **待补** 加厚说明。

---

### 5.3 存储：GPT + FAT（已进表，写路径已拆）

**职责**：发现块设备与卷；FAT 根目录课用读写（8.3）；BLOCKS.DB / Store 元数据。不做完整 POSIX。  

**设计原理**：Volume 抽象多盘；FAT 写路径按现网拆成 Slot（目录项扫描）与 Write/MakeDirectory/Delete/Rename，避免 Mutation 巨石；vvfat 下删除只标 `0xE5`、不强制回收簇（与现网同策，保证 QEMU 宿主可见）。  

**运行逻辑**：

1. `FileSystemInitialize` → `HalBlockInitialize` → `VolumeMountAll`  
2. 认 BLOCKS 卷 → `DataBaseInitialize`  
3. 运行时：Shell/Files/Theme 经 `Fat*` / `Volume*` API  

**写路径内部逻辑**：

```text
路径 → FatPathResolve83 / VolumeResolve
  → FatActiveVolumeOpen
  → FatDirectorySlotFind（Have 或 Free）
  → FatWrite / MakeDirectory / Delete / Rename
  → FatDirectoryEntryPut
```

**依赖关系**：

- 依赖：HalBlock（virtio-blk 等）、BootInfo（无盘时的 OsId 线索）  
- 被依赖：Theme 配置、Store、Elf 加载、Shell FS 命令、Gui Files  
- 禁止：Gui 直接解析 FAT 扇区  

**入口与源码**：`Core/FileSystem/` · Shell `ls/vols/cat/write/mkdir/rm/mv`。  
**与现网**：跟已测通写策略；LFN/深目录/RES **薄/待补**。

---

### 5.4 图形与输入（已进表，Gui 已拆）

**职责**：桌面、窗 Z 序、开始菜单、设置/文件/店客户区；指针点击与拖窗；光标绘制。  

**设计原理**：

- **编排 / 光标 / 指针分离**（对标现网 GuiCursor/GuiPointer，夹内不叠 Gui 前缀）  
- 窗内容脏矩形 Present；光标**只叠前缓冲** save-under，避免烤进背缓冲导致错位  
- 布局描述与 `LAYOUT.CFG` 分离（Layout / LayoutConfiguration）  

**运行逻辑**：

1. `GuiInitialize`：Theme → Layout → Font → Window 桌面 → 可选 CursorEnable  
2. 主循环：`GuiPoll`（Pointer）：抽 HID/PS2 → 命中 Start/窗/图标 → 拖标题栏  
3. 重绘前后 `GuiCursorHide` / `Show` 配对  

**依赖关系**：

- 依赖：Video/Theme/Font、HalUsbHid/HalPs2、FileSystem（Files/Store/Theme 落盘）、Window 几何  
- 被依赖：Console（Shell 窗客户区与焦点吃键）  
- 禁止：Pointer 里写 FAT；Hal 里写窗政策  

**入口与源码**：`Core/Gui/{Gui,Cursor,Pointer,Window*,Layout*,Desktop,Start,Files,Settings,StoreUi}.c`。  
**与现网**：结构对齐拆分；合成/手感 **薄**；输入门面化 **待补**。

---

### 5.5 Shell 与用户态（已进表）

**职责**：串口（及 Shell 窗）命令行；加载运行根目录 ELF；基础进程/文件 syscall。  

**设计原理**：命令注册表 + 分文件 Register；用户态经 syscall，不直链 Core 内部。Process/ElfLoader/页表空间分文件。  

**运行逻辑**：

1. `ConsoleInitialize` → 横幅 + `ShellCommandInitialize`（全域 Register）  
2. `ConsoleRun`：可选 `ProcessRunHello`；循环读行 → `ShellCommandRunLine`  
3. `exec`/双击 ELF：`ElfLoader` → 建空间 → 入口；syscall 回内核  

**依赖关系**：

- 依赖：FS（读 ELF）、Scheduler/Cpu（陷阱）、Gui（焦点与客户区）、可选 Net  
- 被依赖：课堂演示主交互面  
- 禁止：User 程序链接 `Core/*.c`  

**入口与源码**：`Core/Console/`、`User/`。  
**与现网**：演示轨有；COW/pipe/so 等 **待补**。

---

### 5.6 网络（已进表）

**职责**：NIC 上的 Ping/UDP/TCP 课演示；可选 lwIP；配置落盘。  

**设计原理**：HalNet 门面 + Core 协议；Shell 命令分文件注册。Tcp/Ip/Lwip 文件仍偏大，**待拆**后补状态机专节。  

**运行逻辑**：

1. `NetworkInitialize` 找 NIC / 配默认  
2. Shell：`ping`/`net`/`dns`/`tcp*`/`udp*`；`lwip on` 切栈  

**依赖关系**：

- 依赖：HalNet（virtio-net）、Configuration/FS（持久配置）  
- 被依赖：Shell Net 命令、NETDEMO.ELF  
- 禁止：在 Gui 里实现 TCP  

**入口与源码**：`Core/Network/`。  
**与现网**：可演示；DHCP/tray/默认栈 **薄**。

---

### 5.7 USB（部分落地）

**职责**：xHCI 门面 + HID 键鼠（QEMU/课用）。  

**设计原理**：**现网已跑通路径为真相**——只迁结构/命名/夹位，不重写协议猜环。HID 进 `Drivers/UsbHid/`。  

**运行逻辑**：

1. 模块表 `UsbInitialize`  
2. 枚举/配置后 Hid 出键鼠包  
3. `GuiPoll` / Console 读队列  

**依赖关系**：

- 依赖：VMM MapMmio（高址 BAR）、Boot 可选 xHCI 线索  
- 被依赖：Gui/Console 输入  
- 禁止：乱改 TRB/描述符时序「试验」  

**入口与源码**：`Core/USB/Usb.c` · `Hal/X64/Drivers/UsbHid/`。  
**与现网**：跟源；真机 NUC **后置**。

---

### 5.8 主题 / 字体 / DataBase / Store（已进树）

**职责**：课感外观与简单持久化、本地「店」装包。  

**设计原理**：配置进 FAT 文件（`THEME.CFG`/`BLOCKS.DB`/`STORE.CAT`），Gui/Shell 共用 Core API。  

**运行逻辑**：GuiInitialize 载主题字体；Settings 改色落盘；StoreUi/Shell `store` 经 StoreJob。  

**依赖关系**：强依赖 FileSystem；被 Gui/Shell 使用。  

**入口与源码**：`Core/Video/Theme*` `Font*` · `FileSystem/DataBase.c` `Store*.c`。  
**与现网**：主路径有；边角 **待齐**。

---

## 6. 运行与验收

| 动作 | 说明 |
| ---- | ---- |
| `Kernel/./build.sh x64` | 编内核 |
| `Runtime/./smoke-boot.sh` | 冒烟（必要非充分） |
| `Runtime/./run.sh` | 交互 |

能力是否齐：[`Kernel迁移.md`](Kernel迁移.md) 轨 B，不以 smoke 代替。

---

## 7. 文档地图（权威 vs 卫星）

| 文档 | 角色 |
| ---- | ---- |
| **本文** | **权威**：职责/原理/运行逻辑/依赖 |
| [`调用链.md`](调用链.md) | 主路径逐步符号 |
| [`积木原则.md`](积木原则.md) | 硬约束条文 |
| [`Kernel迁移.md`](Kernel迁移.md) | ★ 进度、差缺、归档（不是设计百科） |
| 注释/命名规范 | 源码写法标杆 |

---

## 8. 待补清单（随刀写入四栏）

| 主题 | 缺什么 |
| ---- | ------ |
| PR-B-hal | 各驱动夹内运行逻辑 + 枚举/注册依赖图 |
| PR-B-seq | 「为何此序」加深（与 §3.1 表对齐精修） |
| Tcp/Ip/Lwip | 状态机运行逻辑专节（先拆 ≤300） |
| 用户态 | COW/pipe/so 原理与依赖 |
| 轨 A 现网切片 | 每迁一块新增 §5.x |

---

## 9. 修订记录

| 日期 | 说明 |
| ---- | ---- |
| 2026-10-10 | 初稿 |
| 2026-10-10 | **升格权威文档**：强制四栏（职责/设计原理/运行逻辑/依赖）；重写 §3–§5；钉迁移标杆检查 |
