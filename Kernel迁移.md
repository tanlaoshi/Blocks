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
| **★** | **PR-K2** · `Font` 最小点阵（无 Theme） |
| 排队 | K3 → K4 → …（见 §2） |
| 刚收官 | **PR-K1** · `HalVideo` 最小子集（X64 LFB；手测右上角黄块 ✅） |

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

### 1.3 现网 `KernelMain` 形状（对照）

```text
KernelMain
  ├─ KernelAttachEarlyVideo()     Serial + HalVideoSet(+ Font/Theme/GopEnable)
  ├─ KernelModulesRun()           gModulesFull[] / Virt / VirtDesktop
  ├─ KernelAfterModules()         关 GOP 镜像等
  └─ 起 shell/gui/worker → SchedulerStart
```

模块表（Full，日志名）：

`Serial → Memory → Driver → VirtualMemory → Video → Cpu → SerialEarly → Smp → USB → FileSystem → Network → Gui → Scheduler → Console`

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
| **K9+** | USB / FS / Gui / Network … | 按现网表推进 | **X64 到桌面** 分刀验收 |

> K1–K3 偏「早期可见」；K4 起正式进模块表肉。可按风险微调顺序，但 **K0 必须先落地**。

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
| `Core/HalCapability.c` | FB / ConsoleOnly / VirtSerial / CpuPark |
| `Core/HalVideoStub.c` | `HalVideoSet` 等薄存储 |

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
| `Core/HalVideoStub.c` | 非 X64：Set + 空 Draw/Fill |
| `Core/KernelMain.c` | 打分辨率 + 右上角自检色块 |

---

## 4. 目录预期（随刀增长）

```text
Kernel/
  Core/
    KernelMain.c
    KernelModules.c
    BootInfo.c
    HalCapability.c      # K0：能力旗标
    HalVideoStub.c       # K0：薄存储；K1 起被真实现替换/加厚
  Include/Core/
    Module.h
    …
  Hal/<Arch>/HalSerial.c # 已有
  Hal/<Arch>/…           # K1+ Video/Cpu/…
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
