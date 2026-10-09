# Blocks · KernelMain 以后迁移

> **性质**：`KernelMain` 起至「X64 可跑桌面」的排期与 PR 拆分。  
> **工作区**：`~/Blocks` · 仓：`git@github.com:tanlaoshi/Blocks.git`  
> **接棒已收口**：见 [`Boot迁移.md`](Boot迁移.md) §1.5（接棒干净 ✅；彻底干净 = X64 到桌面）。  
> **积木**：见 [`积木原则.md`](积木原则.md)——迁每一层都盯「可替换面 vs 胶水」。  
> **对照源**（只读）：`~/ToyOS/ToyKernel` / edk2 内 ToyKernel；路径冲突以 Blocks 为准。

---

## 0. ★ 当前刀

| 项 | 值 |
| -- | -- |
| **★** | **PR-K14** · USB xHCI 复位 + 端口状态 |
| 排队 | K15…K20（见下方「加厚到桌面」） |
| 刚收官 | **PR-K13** · Map MMIO；`VMM: map mmio ok` + `Usb: … cap=` ✅ |
| 顺手（不推 ★） | 表序改为 `Memory → VirtualMemory → Driver`（PMM-VMM-Driver） |

> ★ 只跟功能刀（K0…）走；目录收拾单独入库。  
> **K0–K12 = 模块表挂齐（薄实现）**；自 K13 起进入 **加厚到桌面**（见下表），不再只挂空壳。

### 加厚到桌面（K13+ 排队 · 一句话）

> 终局仍是 §1.1「QEMU/真机可跑桌面」。下表是固定排队；**JX 只认文首 ★**，细则在对应 Kx 规划节（未轮到可只留一句话）。

| PR | 一句话 | 为何排这里 | 验收（口述） |
| -- | ------ | ---------- | ------------ |
| **K13** ✅ | VMM：把高址 MMIO（如 xHCI BAR）映进页表 | K10 只能 handoff，读寄存器会挂 | `VMM: map mmio ok`；`Usb: … cap=` ✅ |
| **K14** ★ | USB：xHCI 复位 + 端口状态（仍不 HID） | 有 MMIO 才能摸控制器 | `Usb: port N CCS=…`；软成功不卡死 |
| **K15** | USB HID 键盘（或 QEMU 先 PS/2）最小 | 桌面要键入；串口壳可先吃键 | 按键串口/屏有码；无设备不卡 |
| **K16** | FileSystem：内核侧 Block+FAT 读 `TOYOS.ID` | 去掉对 Boot 扫卷的依赖 | `Fs: TOYOS.ID ready (kernel)`；读一小文件可选 |
| **K17** | Gui：鼠标光标 + 桌面点击反馈 | 从「画皮」到可指点 | 光标移动；点击顶栏有串口/屏反馈 |
| **K18** | Scheduler：LAPIC 定时器 + 协作/轻抢占 | 现网桌面要节拍；Console 可 yield | 周期性 tick 日志或光标闪；shell 仍活 |
| **K19** | 用户态：加载并跑一个 `HELLO.ELF` | 到桌面课感；RootFs 已有 ELF | 串口见 hello；进程退出回 `Blocks>` |
| **K20** | Network：virtio-net 最小收发（如 ARP/ping 一侧） | 表上有网卡但不会说话 | 一次 TX/RX 成功日志；不接 lwIP 全栈 |

**明确后置（勿插队占 ★）**：完整 Theme/TTF、窗管/开始菜单、lwIP/Socket、Store、SMP、真机多驱动边角——现网对照迁入时再拆刀。

### K5 规划（已收官 ✅ · 曾 ★）

**一句话**：`Driver` 空壳 + `VirtualMemory` 认领 4GiB 恒等分页；禁整目录搬家。

| 项 | 定调 |
| -- | ---- |
| **Driver** | `DeviceInitialize` / `Enumerate` 薄壳；不 Probe |
| **VMM** | 认领 `EarlyIdentity`；确认 FB 在窗内；分页后写一像素 |
| **验收** | `[Mod] Driver` → `[Mod] VirtualMemory` → `VMM: FB pixel after PG ok` → `park` ✅ |

表序：`Serial → Memory → Driver → VirtualMemory`。

### K6 规划（已收官 ✅ · 曾 ★）

**一句话**：模块表挂 `Video`；可选背缓冲 + `Present`；**不**强制全屏 Clear（接 Boot 黑底）。

| 项 | 定调 |
| -- | ---- |
| **做** | `InitializeVideo`；X64：`HalVideoInitializeBackbuffer`（PMM 页）+ `Present` 整屏 blit；画点默认进背缓冲 |
| **不做** | 脏矩形、Theme、缩放、整夹搬现网 Video |
| **Arm/RiscV** | Init 成功桩 |
| **验收** | `[Mod] Video`；`Video: backbuffer on` / `present ok` → `park` ✅ |

表序：`… → VirtualMemory → Video`。

### K7 规划（已收官 ✅ · 曾 ★）

**一句话**：模块表挂 `Cpu`；X64 装最小 GDT + IDT（门可挂）；**不**开定时器中断风暴。

| 项 | 定调 |
| -- | ---- |
| **做** | `InitializeCpu`；X64：`lgdt` 内核码/数据段 + `lidt`（异常/IRQ 门指向安全 stub）；串口报 ready |
| **不做** | LAPIC 定时器、IOAPIC 路由、TSS/用户段、SMP、整夹搬现网 Arch |
| **Arm/RiscV** | Init 成功桩（本刀不装 GIC/SBI 定时器） |
| **验收** | `[Mod] Cpu`；`Cpu: gdt/idt ok` → `park` ✅ |

表序：`… → Video → Cpu`。

### K8 规划（已收官 ✅ · 曾 ★）

**一句话**：模块表挂 `Scheduler` + `Console`；单核协作壳；串口提示符 + 回显。

| 项 | 定调 |
| -- | ---- |
| **做** | `SchedulerInitialize` 壳；`Console` 横幅/`Blocks>`/读行回显；`\\r`/`\\n` 都行结束 |
| **不做** | 抢占定时器、多核、完整 shell 语法、Gui |
| **验收** | `[Mod] Scheduler` → `ToyOS ready` → `Blocks>`；输入回显 ✅ |

表序：`… → Cpu → Scheduler → Console`（中间 USB/FS/… 后挂）。

### K9 规划（已收官 ✅ · 曾 ★）

**一句话**：模块表挂 `FileSystem`；认系统卷标记 `TOYOS.ID`（本刀吃 Boot handoff）。

| 项 | 定调 |
| -- | ---- |
| **做** | Boot 扫卷写 `ToyOsIdSeen`；Handoff→`BOOT_INFO`；`FileSystemInitialize` 串口报 ready |
| **不做** | 内核侧 AHCI/virtio-blk、完整 FAT API、写文件、Store |
| **Arm/RiscV** | 无 UEFI 扫卷则 stub 软成功 |
| **验收** | `[Mod] FileSystem`；`Fs: TOYOS.ID ready (Boot handoff)` → `Blocks>` ✅ |

表序：`… → Cpu → FileSystem → Scheduler → Console`。

### K10 规划（已收官 ✅ · 曾 ★）

**一句话**：模块表挂 `USB`；认 Boot 交出的 xHCI 基址（窗内可再读 CAP/VER）。

| 项 | 定调 |
| -- | ---- |
| **做** | `UsbInitialize`；`BOOT_INFO.XhciBase`；窗内读 CAPLENGTH/HCIVERSION；窗外只报 handoff |
| **不做** | 完整 XHCI 环/中断风暴、MSC、HID、高址 MMIO Map |
| **Arm/RiscV** | stub 软成功 |
| **验收** | `[Mod] USB`；`Usb: xhci ok …` → 仍回 `Blocks>` ✅ |
| **Runtime** | `run.sh` 加 `-device qemu-xhci`（q35 默认无 xHCI） |

表序：`… → Cpu → USB → FileSystem → Scheduler → Console`。

### K11 规划（已收官 ✅ · 曾 ★）

**一句话**：模块表挂 `Network`；能认出至少一块网卡（PCI 类码或 virtio）。

| 项 | 定调 |
| -- | ---- |
| **做** | `NetworkInitialize`；X64 最小 PCI 配置空间扫 class `0x02`；串口报 nic ok |
| **不做** | 驱动收发包、lwIP、DHCP、Socket API |
| **Arm/RiscV** | stub 软成功 |
| **验收** | `[Mod] Network`；`Net: nic ok …` → 仍回 `Blocks>` ✅ |
| **Runtime** | `virtio-net-pci` + user netdev |

表序：`… → FileSystem → Network → Scheduler → Console`（Gui 后挂）。

### K12 规划（已收官 ✅ · 曾 ★）

**一句话**：模块表挂 `Gui`；有 FB 时画最小桌面壳（底色 + 顶栏 + 标题），无完整 Theme/窗管。

| 项 | 定调 |
| -- | ---- |
| **做** | `GuiInitialize`；FillRect 背景/顶栏；`DrawString` 标题；背缓冲则 Present |
| **不做** | Theme/TTF、窗口管理、开始菜单、鼠标光标合成 |
| **Arm/RiscV** | 无 FB 则 skip 软成功 |
| **验收** | `[Mod] Gui`；`Gui: desktop ok`；GTK 可见顶栏/标题 → 仍回 `Blocks>` ✅ |

表序：`… → Network → Gui → Scheduler → Console`。

### K13 规划（已收官 ✅ · 曾 ★）

**一句话**：VMM 能把 **4GiB 窗外** 的 MMIO 页映成可访问虚址；用 xHCI BAR 验收。

| 项 | 定调 |
| -- | ---- |
| **做** | `VirtualMemoryMapMmio`（2MiB 大页、虚=物）；VMM 预映 `XhciBase`；Usb 读 CAP/VER |
| **不做** | 完整 xHCI 环、通用 PCI BAR 枚举库、IOMMU、UC 属性钉死 |
| **Arm/RiscV** | MapMmio 失败桩 |
| **验收** | `VMM: map mmio ok`；`Usb: xhci ok … cap=…` → `Blocks>` ✅ |

### K14 规划（★ · 最小子集）

**一句话**：在已映射的 xHCI 上做最小复位，并读出至少一个端口的连接状态（CCS）。

| 项 | 定调 |
| -- | ---- |
| **做** | 读 HCSPARAMS 端口数；操作空间复位（或文档允许的最小 stop/run）；串口报 `port N CCS=` |
| **不做** | 设备枚举、HID/MSC、中断环、完整命令环 |
| **Arm/RiscV** | stub |
| **验收** | `[Mod] USB` 后见 `Usb: port …`；无设备 CCS=0 也算过 → `Blocks>` |

依赖：K13 MapMmio + CAPLENGTH。

---

## 1. 目标与硬约束

### 1.1 终局验收（彻底干净）

```text
加电 → OVMF → BOOTX64.EFI → Kernel.elf
  → KernelMain → 模块表 → shell/gui
  → QEMU split（或真机）可跑桌面 / Shell
```

对标现网 `Scripts/run-split.sh` / `smoke-boot.sh`。**不只以「能编过」为准。**

### 1.2 迁移原则

1. **最小子集（方便 review）**：每一刀只迁「能验收的最短路径」；宁多一刀，勿塞整文件/整层。单 PR 以可 diff、可口述验收为准。  
2. **积木优先**（[`积木原则.md`](积木原则.md)）：接口稳定、实现可换；SCHED/MEM/FS 等迁入即对齐 Ops 面。  
3. **三架构可编**：每刀 `./build.sh x64|arm64|riscv` 绿；真实现可先只落主力 Arch，其它保持桩。  
4. **新 `.c` ≤300 行**；大块按已拆文件拆刀迁。  
5. **恒等窗 4GiB**（`IdentityMap.h`）：正式 VMM 必须读同一常量。  
6. 模块表顺序对照技术手册 §2.1；薄实现须标明「可替换面待补」。  
7. **文件名去冗余 `Kernel` 前缀**：落点已在 `Kernel/`（或对照源 `CodeC-Core/Kernel/`）时，勿再 `Kernel`+Rest。  
   - 去：`KernelModules*.c` → `Modules*.c`；`KernelTask.c` → `Task.c`；迁入时同类照办。  
   - 留：本身就叫这个的（`Kernel.c` / `Kernel.h`）；角色专名 `KernelEntry` / `KernelHandoff`（不是「Kernel+Modules」式冗余）。

### 1.3 现网 `KernelMain` 形状（对照）

```text
KernelMain
  ├─ KernelAttachEarlyVideo()     Serial + HalVideoSet(+ Font/Theme/GopEnable)
  ├─ KernelModulesRun()           gModulesFull[] / Virt / VirtDesktop
  ├─ KernelAfterModules()         关 GOP 镜像等
  └─ 起 shell/gui/worker → SchedulerStart
```

模块表（现网 Full，日志名 · 对照）：

`Serial → Memory → Driver → VirtualMemory → Video → Cpu → SerialEarly → Smp → USB → FileSystem → Network → Gui → Scheduler → Console`

**Blocks 当前表**（与现网差：`Memory → VirtualMemory → Driver`，无 SerialEarly/Smp）：

`Serial → Memory → VirtualMemory → Driver → Video → Cpu → USB → FileSystem → Network → Gui → Scheduler → Console`

---

## 2. PR 拆分（一步一步）

| PR | 一句话 | 主要落点 | 验收（本刀） |
| -- | ------ | -------- | ------------ |
| **K0** ★ | `KernelMain` 骨架 + 模块表壳（仅 Serial） | `Core/KernelMain.c` `KernelModules.*`；能力旗标桩；`HalVideoSet` 薄存储 | 三架构可编；串口见 `KernelMain: …`；停在 park |
| **K1** ★ | `HalVideo` 最小子集 | 见 §3bis；**只** X64 LFB：`Set`/`GetSize`/`DrawPixel`/`FillRect`（可加一角色块自检） | 有 FB：屏上可见色块；串口报 `WxH`；Arm/RiscV 仍桩但可编 |
| **K2** | `Font` 最小点阵（无 Theme） | 内建点阵 + `DrawString` 薄封装 | FB 上能打一行 ASCII |
| **K3** | X64 `HalSerial` 屏上 boot 字 | ring→FB 上滚（仍无完整 Theme） | `SCREEN_LOG=1` 时屏上有 boot 行 |
| **K4** | **Memory（PMM）** | `PhysicalMemory*`；模块表挂上 | `[Mod] Memory`；Regions 可分配 |
| **K5** | Driver 框架 + VirtualMemory | 页表/开分页；沿用 4GiB 窗 | 分页后串口仍活；FB 已映 |
| **K6** | Video 模块（背缓冲） | `InitializeVideo` | 接 Boot 黑底、不强制全屏 Clear |
| **K7** | Cpu（GDT/IDT/LAPIC 或 arch 等价） | `Hal/X64` 等 | 中断门可挂；定时器可空 |
| **K8** | Scheduler 最小 + Console 串口壳 | 单核 shell 可交互 | 串口提示符；`ToyOS ready` 类横幅 |
| **K9** | FileSystem 识盘（Boot handoff） | `ToyOsIdSeen` | `Fs: TOYOS.ID ready` |
| **K10** | USB 探控（XhciBase） | handoff / 窗内 CAP | `Usb: xhci ok` |
| **K11** | Network 探网卡 | PCI class 0x02 | `Net: nic ok` |
| **K12** | Gui 桌面壳 | 底色+顶栏 | `Gui: desktop ok` |
| **K13…K20** | 加厚到桌面 | 见文首「加厚到桌面」表 | 分刀验收；终局 §1.1 |

> K1–K3 偏「早期可见」；K4–K12 挂齐模块表薄实现；**K13 起加厚**。可按风险微调，但 **K0 必须先落地**；改排队须改文首表。

---

## 3. PR-K0 细则（本刀）

### 3.1 做

- `KernelMain`：`BootInfoSet` →（X64）`EarlyIdentity` → `AttachEarly`（再 Init 串口；`HalVideoSet` 只存配置）→ `KernelModulesRun`（仅 Serial）→ 串口报完成 → park。  
- `MODULE` / `ModulesRun` / `InitializeSerial` 薄壳。  
- `HalHasFrameBuffer` / `HalConsoleOnly` / `HalPlatformIsVirtSerialConsole` 最小实现（读 `BootInfo`）。  
- `HalVideoSet` / `GetSize` / `FrameBuffer*` 薄存储（无画像素）。

### 3.2 不做

- 真画屏、Font、Theme、GOP 镜像  
- PMM / VMM / 调度 / FS / USB  
- Runtime / QEMU 脚本

### 3.3 验收清单

- [x] `./build.sh x64|arm64|riscv`  
- [x] 串口可见 `KernelMain:` / `[Mod] Serial` / `modules done (K0)`（本机编通；QEMU 随 Runtime）  
- [x] 文档 ★ 指向下一刀 **K1**

### 3.4 本刀落点（已入库）

| 文件 | 作用 |
| ---- | ---- |
| `Core/KernelMain.c` | AttachEarly + RunFull + park |
| `Core/Module.c` `Include/Core/Module.h` | `ModulesRun` |
| `Core/KernelModules.c` | Full 表仅 Serial |
| `Hal/Common/HalCapability.c` | FB / ConsoleOnly / VirtSerial / CpuPark |
| `Hal/Common/HalVideoStub.c` | `HalVideoSet` 等薄存储 |

---

## 3bis. PR-K1 细则（★ 下一刀 · 最小子集）

### 做

- X64：`Hal/X64/HalVideo.c`（或拆极薄两文件）实现  
  `HalVideoSet` / `GetSize` / `FrameBuffer*` / `DrawPixel` / `FillRect`  
  （线性 FB、假定 32bpp；无后缓冲、无 Present、无字库）  
- `KernelAttachEarly`：有 FB 时串口打 `WxH`，可选右上角画一小色块作自检  
- Arm64/RiscV：继续空操作桩（或仍走 `HalVideoStub`），保证三架构可编  

### 不做（留给 K2+）

- Font / Theme / `DrawString`  
- 后缓冲、脏矩形、GOP 镜像 / `HalSerialGop*`  
- `Hal/Common/RamFrameBuffer` 真实现  
- 模块表挂 `Video`、PMM  

### 验收

- [x] `./build.sh x64|arm64|riscv`（JX 编通）  
- [x] `Runtime/run.sh --headless`：串口见 `FB 1024x768` / `video self-test` / `park`  
- [x] 落点：`Hal/X64/HalVideo.c`；Arm/RiscV 仍 `HalVideoStub`  

### 本刀落点

| 文件 | 作用 |
| ---- | ---- |
| `Hal/X64/HalVideo.c` | Set / GetSize / DrawPixel / FillRect（直写 LFB） |
| `Hal/Common/HalVideoStub.c` | 非 X64：Set + 空 Draw/Fill |
| `Core/KernelMain.c` | 打分辨率 + 右上角自检色块 |

---

## 3ter. PR-K2 细则（已收官 ✅）

### 做

- 内建 8×8 ASCII（`0x20`–`0x7E`）点阵 + `HalVideoDrawCharAt` / `DrawStringAt`
- `KernelAttachEarly`：有 FB 时左上角打 `Blocks K2`（白字）
- 无 Theme、无平滑、无 UTF-8

### 验收

- [x] `./build.sh x64|arm64|riscv`
- [x] 串口：`font self-test (DrawString)` / `modules done; park`
- [x] 屏上左上角可见 `Blocks K2`

### 落点

| 文件 | 作用 |
| ---- | ---- |
| `Include/Core/FontGlyph8x8.h` | 点阵表 |
| `Core/Font.c` | 栅格化 → `DrawPixel` |
| `Core/KernelMain.c` | Font 自检一行 |

---

## 3quater. PR-K3 细则（已收官 ✅）

### 做

- X64 `HalSerialGop.c`：`SCREEN_LOG=1` 时按 `TOY_SCREEN_LOG_*` 上滚到 FB（Y≥80）
- `KernelMain` 主线走 `TOY_SLOG_BOOT`；`HalSerialGopEnable` 接在 `HalVideoSet` 后
- `build.sh`：`SCREEN_LOG=0|1`（默认 0）；Arm/RiscV 仍空操作

### 不做

- 完整 Theme / Present / Desktop ring 叠画

### 验收

- [x] `./build.sh x64|arm64|riscv`；`x64 SCREEN_LOG=1`
- [x] 串口仍见 `KernelMain:` / `park`
- [x] GUI：`SCREEN_LOG=1` 时屏上有 boot 白字行

### 落点

| 文件 | 作用 |
| ---- | ---- |
| `Hal/X64/HalSerialGop.c` | 上滚 / Enable / Mute / Mirror |
| `Hal/X64/HalSerial.c` | WriteChannel → TryMirror |
| `Core/KernelMain.c` | BOOT 通道 + GopEnable |
| `build.sh` | `-DTOY_SCREEN_LOG=` |

---

## 3quinq. PR-K4 细则（已收官 ✅）

### 做

- `PhysicalMemory*`：恒等窗内最大 Free 区 + 位图 first-fit；抠 Handoff 保留段
- 模块表挂 `Memory`；Init 内 alloc/写/free 自检
- **不**抽 `MEMORY_OPS`（默认实现先稳）

### 验收

- [x] `./build.sh x64|arm64|riscv`
- [x] 串口：`[Mod] Memory` / `PMM: self-test ok` / `park`

### 落点

| 文件 | 作用 |
| ---- | ---- |
| `Include/Core/PhysicalMemory.h` | 对外契约 |
| `Core/PhysicalMemory.c` | 单区位图 PMM |
| `Core/KernelModules.c` | Serial → Memory + 自检 |

---

## 4. 目录预期（随刀增长）

```text
Kernel/
  Core/
    Kernel.c             # 入口函数 KernelMain（文件名本身就是 Kernel，不去前缀）
    Modules.c            # 表 + ModulesRun（勿再叫 KernelModules）
    BootInfo.c
  Hal/Common/
    HalCapability.c      # 能力旗标（跨 Arch）
    HalVideoStub.c       # 非 X64 薄视频；X64 用 Hal/X64/HalVideo.c
    HalCpuStub.c
  Include/Core/
    Module.h Modules.h Kernel.h
    …
  Hal/<Arch>/HalSerial.c # 已有
  Hal/<Arch>/KernelEntry.S KernelHandoff.c  # 角色专名，保留
  Hal/<Arch>/…           # Video/Cpu/…
```

---

## 5. 相关入口

| 文档 | 用途 |
| ---- | ---- |
| [`Boot迁移.md`](Boot迁移.md) | 接棒边界；干净定义 |
| [`Kernel/README.md`](Kernel/README.md) | 编译与现状 |
| 现网 `CodeC-Core/Kernel/Kernel.c` | `KernelMain` 对照 |
| 现网技术手册 §2.1 | 模块表顺序 |

---

## 6. 修订记录

| 日期 | 说明 |
| ---- | ---- |
| 2026-10-08 | 初稿：KernelMain 以后计划 + PR-K0…K9+；★ = K0 |
| 2026-10-08 | PR-K0 落地；★ → K1 |
| 2026-10-08 | 挂钩 [`积木原则.md`](积木原则.md)；原则 #1 = 积木优先 |
| 2026-10-08 | 工作区改名 **Blocks**；Kernel 已迁源码补【初学者】注释 |
| 2026-10-08 | 非迁移文档去掉「从哪迁来」表述；`Ramfb`→`RamFrameBuffer` |
| 2026-10-08 | TG：K0+三架构 HalSerial+Blocks 命名+`Hal/Common`；★ 仍为 K1 |
| 2026-10-08 | 原则钉「最小子集」；K1 收窄为 X64 LFB DrawPixel/FillRect |
| 2026-10-08 | JX：K1 代码落地（待 QEMU 手测色块） |
| 2026-10-08 | Runtime：`Esp`/`RootFs`/`Fw` + `run.sh`；headless 见 FB/self-test/park |
| 2026-10-08 | TG：K1 + Runtime QEMU；手测黄块 ✅；★ → K2 |
| 2026-10-08 | TG：K2 Font 8×8 + DrawString；左上角 `Blocks K2` ✅；★ → K3 |
| 2026-10-08 | TG：K3 HalSerialGop 屏上 boot 字；`SCREEN_LOG=1` ✅；★ → K4 |
| 2026-10-08 | TG：K4 最小位图 PMM；`[Mod] Memory` / self-test ok ✅；★ → K5 |
| 2026-10-08 | `HalCapability` / `HalVideoStub` 从 `Core/` 迁入 `Hal/Common/`（Hal 前缀不再混在 Core） |
| 2026-10-08 | TG：K5 Driver 壳 + VMM 认领 EarlyIdentity；★ → K6（含 K6 规划） |
| 2026-10-08 | TG：K6 Video 背缓冲 + BootInfo 分层；★ → K7（含 K7 规划） |
| 2026-10-08 | BootPkg：非入口源文件去掉 `Boot` 文件前缀（`Serial`/`Video*`/`LoadKernel`/…） |
| 2026-10-08 | TG：K7 Cpu GDT/IDT；`KernelMain.c`→`Kernel.c`；`Module.c`→并入 `KernelModules`；★ → K8 |
| 2026-10-08 | 钉原则 §1.2#7：路径已有 Kernel 则去文件名冗余前缀；`KernelModules`→`Modules` |
| 2026-10-08 | TG：K8 Scheduler+Console；去开机自检色块；★ → K9（FS 识盘） |
| 2026-10-08 | TG：K9 FileSystem（Boot `TOYOS.ID` handoff）；★ → K10（USB） |
| 2026-10-09 | TG：K10 USB（`XhciBase` handoff / 窗内 CAP）；★ → K11（Network） |
| 2026-10-09 | TG：K11 Network（PCI class 0x02）；★ → K12（Gui） |
| 2026-10-09 | TG：K12 Gui 桌面壳；钉 K13…K20「加厚到桌面」排队；★ → K13（Map MMIO） |
| 2026-10-09 | TG：K13 VMM MapMmio + Usb 读 CAP；★ → K14（xHCI 端口） |
| 2026-10-09 | 表序：`Memory → VirtualMemory → Driver`（Driver 不再夹在 PMM/VMM 之间） |
