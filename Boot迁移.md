# Blocks · Boot 迁移说明

> **性质**：迁移进度与接棒规格（只写文档，本文件对应刀**不改代码**）。  
> **位置**：Blocks **仓库根**（`~/Blocks/Boot迁移.md`）。  
> **工作区**：`~/Blocks` · GitHub 仓名待改（远程暂 `tanlaoshi/OpenBox`）  
> **对照源**（只读）：旧三仓 / `edk2` 下 ToyBoot、ToyKernel；路径冲突时以 **Blocks 现树** 为准。  
> **结构拍板**：`ToyKernel/Documents/开发/目录结构-ToyOSNew.md`（Blocks 大目录）；Boot 白话说明见 [`Boot/README.md`](Boot/README.md)。

---

## 0. 一句话目标

三种架构在进 **`KernelMain` 之前** 都必须拿着同一份 **`BOOT_INFO`**；  
从调用 `KernelMain(const BOOT_INFO *)` 起，**操作系统主体合流**（调度、FS、桌面等走同一套 Common）。

### 谁调用 `KernelMain`？

| 架构 | 调用方 | 参数 |
| ---- | ------ | ---- |
| **Arm64** | `Kernel/Hal/Arm64`：`KernelEntry` → `KernelHandoff` | `KernelHandoff` 刚填好的 `BOOT_INFO *` |
| **RiscV** | `Kernel/Hal/RiscV`：同上（仅 BSP 跑 `KernelHandoff`） | 同上 |
| **X64** | `Kernel/Hal/X64`：`KernelEntry` → `KernelHandoff` | 从 `UEFI_BOOT_CONFIG` 转出的 `BOOT_INFO *`，再 `KernelMain(&Info)` |

Boot 的 `JumpToKernel` **不**直接调 `KernelMain`——它只跳到内核入口并递上 `UEFI_BOOT_CONFIG`。

| 架构 | Boot 产物 | 进 `KernelMain` 前谁填 `BOOT_INFO` |
| ---- | --------- | ---------------------------------- |
| **Arm64** | `Kernel/Hal/Arm64/Build/*.o`（链进 `Kernel.elf`） | **Boot**：`KernelHandoff` 直接填 |
| **RiscV** | `Kernel/Hal/RiscV/Build/*.o`（同上） | **Boot**：BSP 上 `KernelHandoff` 直接填 |
| **X64** | `Boot/Build/BOOTX64.EFI` | **接棒层**（HAL）：`UEFI_BOOT_CONFIG` → `BOOT_INFO`（见 §3） |

**接棒刀已完成（2026-10-08）**：三架构均可编出 `Kernel/Build/<Arch>/Kernel.elf`。  
其后骨架见 [`Kernel迁移.md`](Kernel迁移.md) **PR-K0**（已落地）。  
`cd Kernel && ./build.sh x64|arm64|riscv`。

---

## 1. 已完成（Boot 柱）

下列均已在 Blocks 落地，可编、可对照文档；交叉链本机在 `Tools/Extract/`（不进 git）。

### 1.1 仓库与工具

| 项 | 落点 | 说明 |
| -- | ---- | ---- |
| 单仓骨架 | `Boot/` `Kernel/` `Runtime/` `Tools/` | 无顶层 Scripts/Config |
| X64 UEFI 工具包 | `Boot/EDK2/` | 裁剪 EDK2，剥独立 `.git` |
| Arm64/RiscV 交叉编译器 | `Tools/Extract/xpack-*-gcc-*/` | Boot **与 Kernel 共用**；本机复制进树，勿软链仓外 |
| 工具说明 | [`Tools/README.md`](Tools/README.md) | 下载/复制步骤 |

### 1.2 Boot（原 ToyBoot）

| 项 | 状态 |
| -- | ---- |
| 源码在 **`Boot/BootPkg/`**（真实包；含 `Video/`） | ✅ |
| `UefiBootConfig.h` → 结构体 **`UEFI_BOOT_CONFIG`**（632 字节，assert 钉死） | ✅ |
| `build.sh`：`WORKSPACE=Boot`，`PACKAGES_PATH=Boot:EDK2` → `Build/BOOTX64.EFI` | ✅ |
| **无** `EDK2/ToyBoot` 软链（包不嵌进裁剪树） | ✅ |
| 编排：`UefiMain` → 串口/显卡/装核/ACPI·xHCI → `ExitBootServices` → `JumpToKernel(&UEFI_BOOT_CONFIG)` | ✅ |
| 白话说明 | [`Boot/README.md`](Boot/README.md) |

X64 HAL 接棒已迁入：`Kernel/Hal/X64/KernelHandoff.c`（见 §3）。

### 1.3 Kernel/Hal/Arm64 · Kernel/Hal/RiscV

| 项 | Arm64 | RiscV |
| -- | ----- | ----- |
| `Boot.S` + `Boot.c` + 本目录 `BootInfo.h` / `BootTypes.h` | ✅ | ✅ |
| `build.sh` → `Build/Boot_asm.o` `Boot.o`（只依赖本目录头 + `Tools/Extract`） | ✅ | ✅ |
| 填 `BOOT_INFO` 后调用 `KernelMain(Info)` | ✅（声明/调用约定） | ✅ |
| 教学向详细注释 | ✅ | ✅（含 amoswap BSP / `SecondaryPark`） |
| README | [`Arm64/README.md`](Kernel/Hal/Arm64/README.md) | [`RiscV/README.md`](Kernel/Hal/RiscV/README.md) |

说明：Arm/RiscV 的 `BootInfo.h` 与 `Kernel/Include/Core/BootInfo.h` **布局须字节级一致**（三份同步）。Boot 目录自带副本，保证 Boot **单独可编**、不 `#include` Kernel。

### 1.4 Kernel 接到 `KernelMain`（本刀 ✅）

| 项 | 落点 | 备注 |
| -- | ---- | ---- |
| `BootInfoSet` / `Get` | `Kernel/Core/BootInfo.c` | ✅ |
| `KernelMain` **骨架（K0）** | `Kernel/Core/KernelMain.c` | AttachEarly + Modules[Serial]；其后见 [`Kernel迁移.md`](Kernel迁移.md) |
| X64 `KernelHandoff` | `Kernel/Hal/X64/KernelHandoff.c` | 原 `Startup.c` 填表逻辑 |
| Arm64/RiscV 链接 | `Kernel/build.sh` + `Hal/*/link.ld` | 链入 `Kernel/Hal/<Arch>/` |
| RiscV `SmpStub` | `Kernel/Hal/RiscV/SmpStub.c` | 次核 park 符号 |
| 完整 Core / Library / Services | — | **未迁**（`KernelMain` 以后） |

### 1.5 干净边界（迁 `KernelMain` 以后时一直盯）

| 层次 | 状态 | 含义 |
| ---- | ---- | ---- |
| **接棒干净** | ✅ | 命名/布局/职责：`BootPkg` → `UEFI_BOOT_CONFIG` → `KernelEntry` → `KernelHandoff` → `BOOT_INFO` → `KernelMain`；无软链包名；Boot 不编 Kernel；Core 不认 UEFI 类型 |
| **彻底干净** | ❌ 尚未 | 整条 **X64：加电 → OVMF → BOOTX64.EFI → Kernel.elf → 模块表 → 可跑桌面/Shell**（对标现网 QEMU split / 真机冒烟） |

**后续迁移硬约束**：每迁一层（Hal / Core / Library / Services / Gui…）都要对照现网，问「这条 X64 开机链会不会断、桌面还会不会起来」——接棒已收口不等于整机已绿。验收最终以 **X64 到桌面** 为准，不只以「能编过 / 进了 KernelMain」为准。

当前接棒侧仍故意留的最小桩（完整 HAL 迁入后替换，不算接棒脏）：

- `Hal/X64/PlatformStub.c`：`HalPlatformSet*` 空吞  
- `KernelMain` 空转（X64 仅 EarlyIdentity 后 `for(;;)`）  
- X64 `HalSerial` 尚无 GOP ring/上屏（仅 COM1 TX；与现网完整路径比仍缺一截）

**串口（三架构齐）**：`Hal/<Arch>/HalSerial.c` + `Include/Core/ToySerialConfig.h`；`KernelHandoff` 里 `Initialize` + `handoff: BOOT_INFO ready`。X64 Boot 侧另有 `BootPkg/BootSerial`（EBS 前）。

---

## 2. 三架构对照（统一点）

```text
X64:    UEFI → BOOTX64.EFI → JumpToKernel(UEFI_BOOT_CONFIG*)
              → KernelEntry → KernelHandoff → BOOT_INFO
              → KernelMain(Info)          ← 统一点

Arm64:  QEMU -kernel → KernelEntry → KernelHandoff → BOOT_INFO
              → KernelMain(Info)          ← 统一点

RiscV:  OpenSBI → KernelEntry（BSP）→ KernelHandoff → BOOT_INFO
              → KernelMain(Info)          ← 统一点
```

> 命名：三架构 ELF 入口均为 **`KernelEntry`**（`Kernel/Hal/<Arch>/`）；填表函数为 **`KernelHandoff`**；链接脚本一律 `ENTRY(KernelEntry)`。

| 步骤 | X64 | Arm64 / RiscV |
| ---- | --- | ------------- |
| 加载内核映像 | Boot 自己读 ELF | 加载器已放好整颗 ELF |
| Boot 第一份材料 | `UEFI_BOOT_CONFIG` | 无（直接 `BOOT_INFO`） |
| 填 `BOOT_INFO` | **`Hal/X64/KernelHandoff`** | **`Hal/<Arch>/KernelHandoff`** |
| `KernelMain` | 桩已链上；完整实现后续再迁 | 同左 |

---

## 3. 已完成刀：三架构推进到 `KernelMain`（其后不迁）

### 3.1 已达成

1. X64：`KernelEntry` → `KernelHandoff(UEFI_BOOT_CONFIG*)` → 填 `BOOT_INFO` → `KernelMain` 桩；`ENTRY(KernelEntry)`。  
2. Arm64/RiscV：`KernelEntry` → `KernelHandoff` → `KernelMain` 桩；`ENTRY(KernelEntry)`。  
3. 统一角色名 **`KernelHandoff`**；`KernelMain` 仅为桩（`BootInfoSet` + X64 EarlyIdentity 后空转）。  
4. `Kernel/build.sh` 产出 `Build/{X64,Arm64,RiscV}/Kernel.elf`。

### 3.2 对照源（只读，迁时代码从这里拷逻辑）

| 现网文件（ToyKernel） | 职责 | Blocks 拟落点（本刀） |
| -------------------- | ---- | --------------------- |
| `CodeA-HAL/X64/Startup.c` | `KernelEntry` → 早期栈 → `BootInfoFromUefi` → `BootInfoSet` →（现）`KernelMain` | `Kernel/Hal/X64/`（如 `Startup.c` / `KernelHandoff.c`） |
| `CodeA-HAL/X64/BootConfig.h` | 与 Boot 交接包镜像 | 与 `Boot/UefiBootConfig.h` 同步；Kernel 侧可 `#include` 镜像头或薄包装 |
| `Include/Core/BootInfo.h` + `BootInfo.c`（`BootInfoSet`） | 清单存储 | 已有头：`Kernel/Include/Core/BootInfo.h`；缺 `.c` 则本刀补最小实现 |
| `Boot/UefiBootConfig.h` | 权威 `UEFI_BOOT_CONFIG` | **已在 Blocks**；改布局须双端一起改 |

### 3.3 本刀必须带走的逻辑要点（勿丢）

从 `Startup.c` 迁出时注意真机约束（注释已写在现网）：

- UEFI 栈可能在 **>512MB**；开自有分页前须切到内核 BSS 内 **早期栈**（`gEarlyStack`）。  
- `UEFI_BOOT_CONFIG` / MemoryMap 缓冲可能在高址：先 **拷到内核 BSS** 再解析。  
- `BootInfoFromUefi`：帧缓冲、VideoModes、GopProtocol、Conventional 内存 → Regions；保留页（含内核映像、交接块、FB、AP trampoline 区等）。  
- 现网顺带调用的 `HalPlatformSetXhciFallback` / `SetRsdp` / `SetSystemTable` / `NoteRuntimeRange`：本刀可先 **桩** 或最小落盘，**不以**拉齐整棵 HAL 为验收条件。  
- 调用约定：Boot 侧可能 MS ABI（RCX）或 SysV（RDI）——现网两种都认，迁入保留。

### 3.4 本刀明确不做

| 不做 | 原因 |
| ---- | ---- |
| 完整 `KernelMain` / Modules / 桌面 / FS | **后续计划待定** |
| 整棵 CodeA–E / Library / Services 迁入 | 超出接棒范围 |
| 改 Arm64/RiscV Boot 行为 | 本刀只链到 `KernelMain` 桩，不改 Boot 逻辑 |
| 把 `BOOT_INFO` 组装挪回 `BOOTX64.EFI` | 保持 UEFI Boot 只交 `UEFI_BOOT_CONFIG`；转换仍在 HAL 接棒 |
| 本刀改路线图 / 搬 ToyKernel Documents | Blocks 文档新写；旧文档只读对照 |

### 3.5 验收（本刀）

- [x] `Kernel/build.sh x64|arm64|riscv` 均出 `Kernel.elf`  
- [x] 符号：三架构=`KernelEntry`+`KernelHandoff`+`KernelMain`  
- [x] `KernelMain` 为桩；其后模块未迁  
- [x] `Boot` 仍可单独编 EFI；接棒在 `Kernel/Hal/X64`  
- [ ] 端到端 QEMU/真机冒烟（可选；本刀以可链为准）

### 3.6 已落地路径

```text
Kernel/Core/{BootInfo.c,KernelMain.c}
Kernel/Hal/X64/{KernelHandoff.c,BootConfig.h,PlatformStub.c,link.ld}
Kernel/Hal/Arm64/link.ld
Kernel/Hal/RiscV/{link.ld,SmpStub.c}
Kernel/build.sh
```

---

## 4. 已拍板的长期内存策略

| 项 | 旧 ToyKernel x86 | Blocks |
| -- | ---------------- | ------- |
| 早期恒等 | 512MB | **4GiB**（`Include/Core/IdentityMap.h`） |
| X64 换栈 | C 里改 `rsp` + `Continue` | **`KernelEntry.S`** 与 Arm 同套路 |
| 开页表（本阶段） | 完整 VMM+PMM | **`EarlyIdentity`** 静态池；VMM 迁入后须沿用同一窗口常量 |

仍高于 4GiB 的 Runtime/Loader 页继续 `NoteRuntimeRange`，供以后按需映射。

## 5. 后续计划

**`KernelMain` 以后**已拆 PR、落文档：见根目录 [`Kernel迁移.md`](Kernel迁移.md)（★ = PR-K0）。

仍挂在本仓、但不在 Boot 刀内的：

- Runtime（原 ToyImage）迁入与跑盘脚本  
- 端到端 QEMU / 真机 smoke（Boot EFI + Kernel.elf）  
- 旧 `~/ToyOS` / edk2 备份退役  

---

## 5.1 相关入口

| 文档 / 目录 | 用途 |
| ----------- | ---- |
| [`Kernel迁移.md`](Kernel迁移.md) | KernelMain→桌面 PR 排期 |
| [`Boot/README.md`](Boot/README.md) | UEFI 路径与 `UEFI_BOOT_CONFIG` |
| [`Kernel/Hal/Arm64/README.md`](Kernel/Hal/Arm64/README.md) / [`RiscV`](Kernel/Hal/RiscV/README.md) | virt 路径与直接 `BOOT_INFO` |
| [`Tools/README.md`](Tools/README.md) | 交叉链 |
| [`Kernel/README.md`](Kernel/README.md) | Kernel↔Boot 边界 |
| 旧仓 `CodeA-HAL/X64/Startup.c` | 接棒逻辑对照源 |

---

## 6. 修订记录

| 日期 | 说明 |
| ---- | ---- |
| 2026-10-08 | 初稿：汇总已完成 Boot 迁移；钉当前刀为 X64→填完 `BOOT_INFO`、停在 `KernelMain` 前；后续待定 |
| 2026-10-08 | 改放仓库根 `Boot迁移.md`；补「谁调 KernelMain」 |
| 2026-10-08 | 入口符号统一为 **`KernelEntry`**（曾短暂叫 BootEntry；源码在 Kernel/Hal 后改回） |
| 2026-10-08 | `BootHandoff` → **`KernelHandoff`**；头文件 `UefiBootConfig.h`（不再用 Boot 字样作函数名） |
| 2026-10-08 | 填表角色统一为 **`KernelHandoff`**（原 `BootMain` / 拟 `HalGetBootConfig` / 现网 X64 `KernelEntry`） |
| 2026-10-08 | 三架构链到 `KernelMain` 桩：`Kernel/build.sh`；其后未迁 |
| 2026-10-08 | `X64_BOOT_CONFIG` → **`UEFI_BOOT_CONFIG`**（按固件形态命名，便于将来 AArch64 UEFI） |
| 2026-10-08 | 长期：恒等窗 **4GiB**（`IdentityMap.h`）；X64 `KernelEntry.S` 设栈；`EarlyIdentity` 开页表；去掉 C 内改 rsp |
| 2026-10-08 | Arm64/RiscV 入口迁入 **Kernel/Hal/**；顶层 Boot/ 仅 X64 EFI |
| 2026-10-08 | 取消 `Boot/X64` 子层：UEFI 源码与 EDK2 直接落在 `Boot/` |
| 2026-10-08 | **BootPkg** 真实包 + `PACKAGES_PATH`；去掉 `EDK2/ToyBoot` 软链；`CONF_PATH=EDK2/Conf`；README 合并为 `Boot/README.md` |
| 2026-10-08 | 钉干净边界：接棒 ✅；彻底干净 = X64 到桌面（迁 KernelMain 以后一直盯） |
| 2026-10-08 | 后续计划改指 [`Kernel迁移.md`](Kernel迁移.md) |
| 2026-10-08 | 工作区改名 **Blocks**（原 OpenBox）；GitHub 仓名待网页同步 |
| 2026-10-08 | 三架构 `HalSerial`：X64 COM1 / Arm64 PL011 / RiscV 16550；`build.sh` 默认 `TOY_SERIAL=1` |
