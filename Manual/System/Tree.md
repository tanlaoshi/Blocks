# Blocks 目录结构

> **写给**：开箱、找文件、弄清 Boot / Kernel / Runtime 边界。  
> **不是**：逐文件清单（过时的 CodeA–E / ToyOS 三仓叙述已废）。  
> **开机链路**：见同目录 [`Blocks开机流程.md`](Boot.md)。  
> **日常短名**：仓库根 `source ~/Blocks/env.sh` → `build` / `run` / `sync` …

---

## 0. 顶层（单仓）

```text
Blocks/                    # git 根；GitHub: tanlaoshi/Blocks
  env.sh                   # source 后短名（对标 edksetup）
  Boot/                    # 固件侧：拉起 Kernel（X64 主路径 UEFI）
  Kernel/                  # OS 源码与交叉构建
  Runtime/                 # 摆盘 / QEMU / 种子（Esp、RootFs、Store）
  Documents/               # 手册、日档路线图、归档
  Tools/                   # xpack 交叉链等（多半 gitignore，U 盘/rsync）
```

| 目录 | 人话 |
| ---- | ---- |
| **Boot** | 机器怎么把 `Kernel.elf` 拉起来 |
| **Kernel** | OS 怎么写、怎么编 |
| **Runtime** | 编出来怎么摆盘、怎么跑 |

浏览序：`Boot` → `Kernel` → `Runtime`。  
**无**顶层统一 `Build/`：产物在 `Boot/Build/`、`Kernel/Build/`；Runtime 只收同步结果。

---

## 1. Boot

| 项 | 现网 |
| -- | ---- |
| X64 | `Boot/BootPkg/` + 裁剪 `Boot/EDK2/` → `Boot/Build/BOOTX64.EFI` |
| 职责 | GOP、读含 `TOYOS.ID`/`BLOCKS.ID` 卷上的 `Kernel.elf`、ExitBootServices、跳 `KernelEntry` |
| Arm64 / RiscV | **无**本树 UEFI 主路径；virt / 真机走 Kernel 自有 Boot（`-kernel` + DTB / OpenSBI） |

```bash
source ~/Blocks/env.sh
boot          # → Boot/build.sh
```

---

## 2. Kernel（同心圆）

示意：[Kernel-rings.png](../../Documents/项目/Kernel-rings.png)。阅读心智：**先骨后肉**（见开机文档 §0）。

```text
Kernel/
  BringUp/     # <Arch>/Entry·Handoff + KernelMain / Modules
  HAL/         # Hal* 门面、Drivers、Board/<板>/
  Library/     # Fat、VFS、纯库（无业务 Services）
  Core/        # 调度 / PMM / VMM / Process / Syscall / Driver 表…
  Services/    # Shell、Gui、Tasks 四环、Store、Net…
  User/        # Ring3 App、CRT、include/Blocks
  Scripts/     # 真机 sync / virt / smoke / expect（lib/）
  Include/     # 分层契约头
  ThirdParty/  # lwIP 等（肉）
  Build/       # 产物：HAL/<Arch>/Kernel.elf、User/*.elf
  Makefile  build.sh  README.md
```

**依赖（只允许向下）**：`User → Services → Core → Library → HAL → Boot(交棒)`。  
`make check-library-pure` 守 Library 边界。

### 2.1 HAL / 板包

| 路径 | 用途 |
| ---- | ---- |
| `HAL/X64/` | 桌面真机 / QEMU OVMF（键鼠、网、块、核显…） |
| `HAL/Arm64/`、`HAL/RiscV/` | virt + 真机板；**Arch 通用**，板差进 `Board/` |
| `HAL/<Arch>/Board/virt/` | QEMU 默认板 |
| `HAL/Arm64/Board/Raspberry/Pi3BPlus/` | 树莓派 3B+（`BOARD=Raspberry/Pi3BPlus`） |
| `HAL/RiscV/Board/milk-v-duo-s/` | Duo S 串口靶 |
| `HAL/Virtio/` | QEMU virt 共享 |

一板一编：`build arm64 BOARD=…`。约定见 `HAL/Board/README.md`。

### 2.2 Scripts（宿主机工具）

权威在 `Kernel/Scripts/`（`lib/` 实现 + `sync/` `run/` `smoke/` …；原 OpenBox 已拍扁）。  
根 `env.sh` 把常用动作收成短名；**不**与 `Runtime/*.sh` 合成一套：

| 短名 | 去向 |
| ---- | ---- |
| `build` / `boot` | Kernel / Boot `build.sh` |
| `sync` | Runtime 双盘布局 |
| `sync usb` / `sync nuc` | 真机 U 盘 / NUC SSD（TBU / TBN） |
| `run` / `smoke` | X64 QEMU |
| `run-virt-arm` / `run-virt-riscv` | 多架构 virt |
| `prepare-pi-sd` | Pi3BPlus 摆盘目录 |

---

## 3. Runtime

```text
Runtime/
  Esp/X64/EFI/BOOT/BOOTX64.EFI   # disk0
  RootFs/X64/                    # disk1：Kernel.elf、Apps/、Assets、BLOCKS.DB…
  RootFs/Arm64/  RootFs/RiscV/   # virt 用
  Store/                         # 商店货架种子
  Fw/                            # OVMF 变量盘种子等
  sync.sh  run.sh  smoke-boot.sh
```

```bash
source ~/Blocks/env.sh
build && sync && run --headless
```

---

## 4. Documents（手册落点）

| 路径 | 内容 |
| ---- | ---- |
| `手册/` | 接手、应用、驱动、课堂作业、**本目录两文** |
| `路线图MMDD.md` | **活 ★** 日档 |
| `项目/路线图.md` | 暗号 + 归档史（不堆新排期） |
| `过程/` · `柱规格/` | 已完柱与规格 |

---

## 5. Tools

`Tools/xpack-aarch64-*`、`xpack-riscv-*`：**不进 git**（见 [`同步.md`](../../同步.md)）。只做 X64 可用系统 `gcc`。

---

## 6. 与旧名对照（勿再当路径）

| 旧 | 现 |
| -- | -- |
| ToyOS / ToyKernel / ToyBoot / ToyImage | **Blocks** / `Kernel` / `Boot` / `Runtime` |
| `source Scripts/env.sh` | `source ~/Blocks/env.sh` |
| `CodeA-HAL`… 磁盘前缀 | 短名 `HAL/` `Core/`…（分层规则仍在） |
| OpenBox 独立仓设想 | 已拍扁为 `Kernel/Scripts/` + 仓根 `env.sh` |

---

## 7. 修订

| 日期 | 说明 |
| ---- | ---- |
| 2026-10-23 | 合成：取代开箱/OpenBox/ToyKernel 目录长文与骨架文中的「树」部分。 |
| 2026-10-26 | `Kernel/OpenBox` → `Kernel/Scripts` 拍扁。 |
