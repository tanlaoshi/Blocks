# Board：`rpi-3b-plus`（Raspberry Pi 3 Model B+）

> **今日工作目录**（[路线图1023 · rpi-desk](../../../../../Archive/路线图1023.md#rpi-desk)）。  
> Arm64 **真机靶**：厂商/社区 **U-Boot + DTB**（不要 UEFI）。GUI 课堂仍 QEMU `BOARD=virt`。  
> 约定：[`HAL/Common/Board/README.md`](../../../Common/Board/README.md)。

| 项 | 填写 |
|----|------|
| **板名** | Raspberry Pi 3 Model B+ |
| **SoC** | Broadcom BCM2837B0（Cortex-A53 ×4） |
| **Arch** | `Arm64`（`BOARD=rpi-3b-plus`） |
| **固件** | 官方 boot 固件 + 社区/发行版 U-Boot（**不改**厂商树源码） |
| **加载方式** | U-Boot `booti`（见下）；DTB 常用 `bcm2837-rpi-3-b-plus.dtb` |
| **串口** | 见 `BoardConfig.h`：默认按 **PL011 UART0** @ `0x3F201000`；接线与是否改走 mini-UART 见 [`NOTES.md`](NOTES.md) |
| **能力** | 本柱先 **命令行 only**（无 FB）→ 串口壳；HDMI/USB 后置 |
| **课堂关系** | 公司：`BOARD=virt` 冒烟 + 本包**能编**；回家：SD 手测（rpi-2…4） |

---

## 构建

```bash
source ~/Blocks/env.sh

build arm64 BOARD=rpi-3b-plus LWIP=1
# → Kernel/Build/HAL/Arm64/Kernel.elf

# 可选扁平二进制（U-Boot load）：
kernel-bin arm64 BOARD=rpi-3b-plus

# 公司验收公共层：切回 virt 再冒烟
build arm64 BOARD=virt
run-virt-arm --headless

# SD 摆盘目录（不插卡；rpi-2）
build arm64 BOARD=rpi-3b-plus LWIP=1
ck-pi-sd                               # 摆盘 + 刷卡（内含 prepare）
# prepare-pi-sd                        # 仅目录、不插卡（可选）
# ck-pi-sd --dtb=/path/to/bcm2837-rpi-3-b-plus.dtb
```

默认不 source 时仍可 `cd Kernel && ./build.sh …`；日常以短名为准。

---

## 加载约定（U-Boot · 概念）

变量名以板子 `printenv` 为准。摆盘产物见 `prepare-pi-sd` → `README-SD.txt`。推荐 **bootelf**（与 Duo S 同形；本柱不强制 Linux `Image`/`booti`）：

```text
load mmc 0:1 0x20000000 Kernel.elf
load mmc 0:1 ${fdt_addr_r} bcm2710-rpi-3-b-plus.dtb
fdt addr ${fdt_addr_r}
bootelf -p 0x20000000
```

（内核链在 `0x80000`；ELF 文件暂存 `0x20000000`，避免与段重叠。卡上自带 DTB 用 `bcm2710`。）

入口（AArch64）：`x0 = DTB 物理地址`，`x1～x3 = 0`。  
Startup 保存 DTB → 填 `BOOT_HANDOFF` → `KernelMain`。**Common 不解析 DTB。**

---

## 本包文件

| 文件 | 状态 |
|------|------|
| `README.md` | 本文件 |
| `BoardConfig.h` | UART / 加载提示 / 能力勾选 |
| `Board.h` / `Board.c` | 板名骨架；Startup 填充后置 |
| `NOTES.md` | 串口/固件踩坑备忘 |
| `scripts/prepare-sd.sh` | SD 摆盘（`ck-pi-sd` 内调；短名 `prepare-pi-sd` 仅目录） |
| `scripts/ck-pi-sd.sh` | 暗号 CK：prepare + 刷 bootfs/BLOCKS |

---

## 验收（本板）

- [x] `build arm64 BOARD=rpi-3b-plus LWIP=1` 过（rnet-1；公司即可）  
- [x] `prepare-pi-sd` 产出含 `Kernel.elf` + `README-SD.txt`（公司即可）  
- [ ] `build arm64 BOARD=virt` + `run-virt-arm --headless` 不回退（有 QEMU 的机器）  
- [ ] 厂商/社区 U-Boot 不改源码即可加载（回家 · rpi-3）  
- [ ] 串口出现约定横幅（回家 · rpi-3）  
- [ ] `BOOT_HANDOFF.Regions` 与 DRAM 大致一致（回家 · rpi-4）  
- [ ] `git diff`：**Common / Gui 无业务改动**  
- [ ] **未**在本板做 GUI / 桌面（本柱）

等价横幅（若不用默认句）：`Blocks Arm64 Pi3BPlus ready`（待真机钉死）

---

## 已知缺口

- DTB 须自备（`--dtb=`）；脚本不打包官方固件  
- 真机串口 / DTB memory 未手测（rpi-3 / rpi-4）  
- HDMI FB、USB 键鼠、网：后置，不进本柱刀表  

---

## 明确不做

- 重写 BootROM / 厂商 U-Boot 源码  
- Pixel / 全志当本目录主靶  
- 把板级 `#ifdef` 或 DTB 解析放进 Common  
- 无规格堆启发式（缩超时、skip、猜 DRAM）  
