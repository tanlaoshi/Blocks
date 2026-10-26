# Blocks

单仓：Boot · Kernel · Runtime · **Start** · **Archive**。

```text
Blocks/
  Start/       ← 动手入口 + 读者手册（App / Driver / System / Contribute / Course）
  Archive/     ← 日档、过程、柱规格、同步、原则与学习记录（存档）
  Boot/  Kernel/  Runtime/  Tools/
  env.sh
```

| 你要… | 去哪 |
| ------ | ---- |
| **上手 / 写代码 / 读手册** | [`Start/`](Start/README.md) · [`Start/START.md`](Start/START.md) |
| **写 App（毛坯）** | [`Start/App/`](Start/App/) |
| 日档 ★ / 排期 | [`Archive/路线图1026.md`](Archive/路线图1026.md) |
| 从 OVMF 逐段学 | [`Archive/原则与学习记录.md`](Archive/原则与学习记录.md) |
| 换机 / 工具链细表 | [`Archive/同步.md`](Archive/同步.md) |
| GitHub | `git@github.com:tanlaoshi/Blocks.git` |

---

## 需要安装的基本软件（Debian / Ubuntu）

课机 x64 QEMU（最低）：

```bash
sudo apt update
sudo apt install -y build-essential python3 make nasm uuid-dev \
  qemu-system-x86 ovmf
```

| 包 | 用途 |
| --- | --- |
| `build-essential` / `make` | `gcc`/`ld` 编内核与用户 ELF |
| `python3` | 板脚本（Duo `mk-boot-sd.py` / `mk-boot-scr.py`）等 |
| `nasm` | 部分汇编 |
| `uuid-dev` | Boot / 工具依赖 |
| `qemu-system-x86` | `run` → `qemu-system-x86_64` |
| `ovmf` | UEFI 固件（常见 `/usr/share/OVMF/OVMF_CODE_4M.fd`） |

再编 / 跑 **Arm64 / RiscV** 模拟器时追加：

```bash
sudo apt install -y qemu-system-arm qemu-system-misc
# 交叉编译二选一：系统包，或解压 xpack 到 Tools/（见 Archive/同步.md）
sudo apt install -y gcc-aarch64-linux-gnu gcc-riscv64-linux-gnu
```

真机刷卡（Pi / Duo）另需：`mtools` 或能挂载 vfat 的桌面。Pi：`ck-pi-sd`；Duo：`ck-duo-sd`（**勿**对 Duo 跑 `ck-pi-sd`）。应用卷 **BLOCKS** 根含 `Kernel.elf`（三架构对齐）。

---

## 怎么开始（通用）

```bash
cd ~/Blocks && source ./env.sh
```

短名：`build` · `boot` · `run` · `run-virt-arm` · `run-virt-riscv` · `boards` · `ck-pi-sd` · `ck-duo-sd` 等（`source` 后会打印一览）。

---

## 按架构：编译与启动

### X64（课机主路径 · UEFI + QEMU）

| 步 | 命令 | 说明 |
| --- | --- | --- |
| 编内核 | `build` | 默认同步 `Runtime/RootFs/X64` |
| 可选 Boot | `boot` | `BOOTX64.EFI` |
| 启动 | `run` | OVMF + 图形/串口；Guest：`blocks>` |
| 无窗串口 | `run --headless` | 终端里进壳 |

产物：`Kernel/Build/HAL/X64/Kernel.elf`（亦进 RootFs）。App 毛坯见 `Start/App/`。

### Arm64

| 场景 | 编译 | 启动 |
| --- | --- | --- |
| **QEMU virt**（课堂靶） | `build arm64` 或 `build arm64 BOARD=virt` | `run-virt-arm`（可加 `--headless`） |
| **树莓派 3B+** 真机 | `build arm64 BOARD=rpi-3b-plus LWIP=0` | 摆盘：`prepare-pi-sd` → 插卡 **`ck-pi-sd`**（固件→bootfs；**`Kernel.elf`→BLOCKS**）；串口 115200 |

板包列表：`boards arm64`。真机历程见 `Archive/过程/排障/` 下 Pi 档。

### RiscV

| 场景 | 编译 | 启动 |
| --- | --- | --- |
| **QEMU virt** | `build riscv`（默认 virt 类板） | `run-virt-riscv` |
| **Milk-V Duo S** 真机 | `build riscv BOARD=milk-v-duo-s` | **`ck-duo-sd`**：`Kernel.elf` **只**在 LABEL=BLOCKS（boot 上删除）；上电 `boot.sd` FIT 由该文件打包。**禁止** `ck-pi-sd`。串口 115200 |

板包列表：`boards riscv`。Duo 脚本：`Kernel/HAL/RiscV/Board/milk-v-duo-s/scripts/`。

### 对照（一眼）

| | X64 | Arm64 virt | Arm64 Pi3B+ | RiscV Duo S |
| --- | --- | --- | --- | --- |
| 交叉工具 | 宿主 `gcc` | aarch64-*-gcc / xpack | 同左 | riscv64-*-gcc / xpack |
| 模拟器 | `qemu-system-x86_64` + OVMF | `qemu-system-aarch64` | — | `qemu-system-riscv64`（virt） |
| 真机启动 | RootFs 含 `Kernel.elf` | — | U-Boot + `ck-pi-sd`（核→BLOCKS） | 厂商 U-Boot + `ck-duo-sd`（核→BLOCKS；启动仍 `boot.sd`） |
| 典型板旗 | 桌面 + HID | 串口/virt | FB + HID | **串口壳**（无 Duo GUI） |

细节与换机：[`Archive/同步.md`](Archive/同步.md) · [`Start/START.md`](Start/START.md)。
