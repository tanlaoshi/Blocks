# OpenBox · Boot 迁移说明

> **性质**：迁移进度与接棒规格（只写文档，本文件对应刀**不改代码**）。  
> **位置**：OpenBox **仓库根**（`~/OpenBox/Boot迁移.md`）。  
> **工作区**：`~/OpenBox` · 仓：`git@github.com:tanlaoshi/OpenBox.git`  
> **对照源**（只读）：旧三仓 / `edk2` 下 ToyBoot、ToyKernel；路径冲突时以 **OpenBox 现树** 为准。  
> **结构拍板**：`ToyKernel/Documents/开发/目录结构-ToyOSNew.md`（OpenBox 大目录）；Boot 白话说明见 [`Boot/README.md`](Boot/README.md)。

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

**本刀已完成（2026-10-08）**：三架构均可编出 `Kernel/Build/<Arch>/Kernel.elf`，路径停在 **`KernelMain` 桩**（其后模块未迁）。  
`cd Kernel && ./build.sh x64|arm64|riscv`。

---

## 1. 已完成（Boot 柱）

下列均已在 OpenBox 落地，可编、可对照文档；交叉链本机在 `Tools/Extract/`（不进 git）。

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
| `KernelMain` **桩** | `Kernel/Core/KernelMain.c` | 保存清单后空转；其后未迁 |
| X64 `KernelHandoff` | `Kernel/Hal/X64/KernelHandoff.c` | 原 `Startup.c`/`KernelEntry` |
| Arm64/RiscV 链接 | `Kernel/build.sh` + `Hal/*/link.ld` | 链入 `Kernel/Hal/<Arch>/` |
| RiscV `SmpStub` | `Kernel/Hal/RiscV/SmpStub.c` | 次核 park 符号 |
| 完整 Core / Library / Services | — | **未迁**（后续待定） |

---

## 2. 三架构对照（统一点）

```text
X64:    UEFI → BOOTX64.EFI → … → JumpToKernel(UEFI_BOOT_CONFIG*)
              → [接棒] KernelHandoff / 现网 KernelEntry
              → 填 BOOT_INFO（+ BootInfoSet）
              → KernelMain(Info)          ← 统一点

Arm64:  QEMU -kernel → KernelEntry（Boot.S）→ KernelHandoff
              → 填 BOOT_INFO
              → KernelMain(Info)          ← 统一点

RiscV:  OpenSBI → KernelEntry（BSP）→ KernelHandoff
              → 填 BOOT_INFO
              → KernelMain(Info)          ← 统一点
```

> 命名：三架构 ELF 入口均为 **`KernelEntry`**（在 `Kernel/Hal/`）；填表函数为 **`KernelHandoff`**。链接脚本 `ENTRY(KernelEntry)`。

| 步骤 | X64 | Arm64 / RiscV |
| ---- | --- | ------------- |
| 加载内核映像 | Boot 自己读 ELF | 加载器已放好整颗 ELF |
| Boot 第一份材料 | `UEFI_BOOT_CONFIG` | 无（直接 `BOOT_INFO`） |
| 填 `BOOT_INFO` | **HAL `KernelHandoff`** | **`KernelHandoff`（Boot/*.c）** |
| `KernelMain` | 桩已链上；完整实现后续再迁 | 同左 |

---

## 3. 已完成刀：三架构推进到 `KernelMain`（其后不迁）

### 3.1 已达成

1. X64：`KernelHandoff(UEFI_BOOT_CONFIG*)` → 填 `BOOT_INFO` → `KernelMain` 桩；`ENTRY(KernelHandoff)`。  
2. Arm64/RiscV：`KernelEntry` → `KernelHandoff` → `KernelMain` 桩；`ENTRY(KernelEntry)`。  
3. 统一角色名 **`KernelHandoff`**；`KernelMain` 仅为桩（`BootInfoSet` 后空转）。  
4. `Kernel/build.sh` 产出 `Build/{X64,Arm64,RiscV}/Kernel.elf`。

### 3.2 对照源（只读，迁时代码从这里拷逻辑）

| 现网文件（ToyKernel） | 职责 | OpenBox 拟落点（本刀） |
| -------------------- | ---- | --------------------- |
| `CodeA-HAL/X64/Startup.c` | `KernelEntry` → 早期栈 → `BootInfoFromUefi` → `BootInfoSet` →（现）`KernelMain` | `Kernel/Hal/X64/`（如 `Startup.c` / `KernelHandoff.c`） |
| `CodeA-HAL/X64/BootConfig.h` | 与 Boot 交接包镜像 | 与 `Boot/UefiBootConfig.h` 同步；Kernel 侧可 `#include` 镜像头或薄包装 |
| `Include/Core/BootInfo.h` + `BootInfo.c`（`BootInfoSet`） | 清单存储 | 已有头：`Kernel/Include/Core/BootInfo.h`；缺 `.c` 则本刀补最小实现 |
| `Boot/UefiBootConfig.h` | 权威 `UEFI_BOOT_CONFIG` | **已在 OpenBox**；改布局须双端一起改 |

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
| 本刀改路线图 / 搬 ToyKernel Documents | OpenBox 文档新写；旧文档只读对照 |

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

| 项 | 旧 ToyKernel x86 | OpenBox |
| -- | ---------------- | ------- |
| 早期恒等 | 512MB | **4GiB**（`Include/Core/IdentityMap.h`） |
| X64 换栈 | C 里改 `rsp` + `Continue` | **`KernelEntry.S`** 与 Arm 同套路 |
| 开页表（本阶段） | 完整 VMM+PMM | **`EarlyIdentity`** 静态池；VMM 迁入后须沿用同一窗口常量 |

仍高于 4GiB 的 Runtime/Loader 页继续 `NoteRuntimeRange`，供以后按需映射。

## 5. 后续计划（待定）

以下**不排期**：

- `KernelMain` 之后：模块表 / 调度 / FS / 桌面 / 完整 HAL（VMM 正式实现须读 `IdentityMap.h`）  
- Runtime（原 ToyImage）迁入与跑盘脚本  
- 端到端 QEMU / 真机 smoke（Boot EFI + Kernel.elf）  
- 旧 `~/ToyOS` / edk2 备份退役  

有新拍板时追加本节。

---

## 5. 相关入口

| 文档 / 目录 | 用途 |
| ----------- | ---- |
| [`Boot/README.md`](Boot/README.md) | 三架构白话总览 |
| [`Boot/README.md`](Boot/README.md) | UEFI 路径与 `UEFI_BOOT_CONFIG` |
| [`Kernel/Hal/Arm64/README.md`](Kernel/Hal/Arm64/README.md) / [`RiscV`](Kernel/Hal/RiscV/README.md) | virt 路径与直接 `BOOT_INFO` |
| [`Tools/README.md`](Tools/README.md) | 交叉链 |
| [`Kernel/README.md`](Kernel/README.md) | Kernel↔Boot 边界（随本刀更新实现状态） |
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
