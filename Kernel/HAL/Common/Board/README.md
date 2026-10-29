# Board 包约定（PR-B0）

> **一句话**：别人加板只交 `HAL/<Arch>/Board/<board>/`；**Common / Gui / FAT diff 为空**；**不改**厂商 U-Boot / BootROM。  
> 概念（UEFI vs U-Boot、DTB、Startup）：[`Archive/技术手册.md`](../../Archive/技术手册.md)。  
> **逐步操作**：[`Archive/技术手册.md`](../../Archive/技术手册.md)。  
> 排期：[`Archive/路线图.md`](../../Archive/路线图.md) **1.3b**（B0～B3）。**x86 桌面真机见 1.3c，勿与本树混仓。**

---

## 1. 目录落点

```text
HAL/
├── Common/Board/              ← 本文 + _template（约定；真板不交这里）
├── <Arch>/                    # X64 | Arm64 | RiscV
│   └── Board/<board>/         # 真板包（virt / rpi-3b / milk-v-duo-s / …）
├── Virtio/                    # QEMU virt 设备共享
└── Common/                    # 跨 Arch 仅此额外顶栏（Video / HalVideo / Slots / Board 约定）
```

- **Arch vs Board**：MMU / 异常 / IRQ 控制器形状留在 `HAL/<Arch>/`；内存图、串口基址、模块子集、DTB 兼容串在 Board。  
- **设备 vs 板**：声卡等走 Driver（[`驱动框架.md`](../../Archive/技术手册.md)）；板只声明「有这颗设备」并注册，不改 Services。  
- **Makefile `BOARD=`**：**B2 ✅** — `BOARD=virt`（默认）→ `HAL/<Arch>/Board/<board>/`；`make boards`；缺包报错。

---

## 2. 最小文件清单

| 文件 | 必填？ | 职责 |
|------|--------|------|
| `README.md` | **是** | 板名、Arch、固件、加载命令、UART、已知缺口、验收口径 |
| `BoardConfig.h` | 建议 | `UART` 基址、加载/内核链接提示、能力勾选（无 FB → 串口壳） |
| `Board.c` / `Board.h` | B2+ | Startup 调用的填充辅助；兼容 ID 表；**不**进 Common |
| `NOTES.md` | 可选 | 真机踩坑（引脚、分区、厂商环境变量） |
| `dts/` 或链接到厂商 `.dtb` | 可选 | 仅文档/脚本引用；**Common 禁止解析 DTB** |

### 2.1 板级外设文件名（X64 / Arm64 / RiscV 同形）

Naming：全词；协议/总线正式名可保留（`USB`、`SDHCI`）。**三架构同一门面名**，有则实现、无则不交空 stub。

| 能力 | 文件 | 导出（Board 层） | 现状 |
|------|------|------------------|------|
| 板载以太网 | `Ethernet.c` / `Ethernet.h` | `BoardEthernetRegister` / `BoardEthernetProbe` | Duo S ✅（net-4 Bind）；Pi / X64 板包暂无则不交文件 |
| µSD / SDHCI | `Sdhci.c` / `Sdhci.h`（或 Pi `Sdhost.c`） | `BoardSdhciRegister` / `BoardSdhostRegister` | Duo + Pi |
| USB host（Synopsys DWC2 IP） | `UsbHostController.c` / `UsbHostController.h` | `BoardUsbHostControllerProbe` | Duo + Pi |

禁止：`Eth.c`、`Dwmac.c`、`Sd.c`、`Dwc2.c` 等砍短名顶门面（USB 主机用 `UsbHostController`）。

复制模板：

```bash
cp -a HAL/Common/Board/_template HAL/RiscV/Board/milk-v-duo-s   # Duo S 例（PR-B3）；再 ./build.sh riscv BOARD=milk-v-duo-s
# 编辑该目录 README.md / BoardConfig.h，删掉 .example 后缀
```

---

## 3. 贡献者只交什么

**交**

- 本板目录下的 README / Config / Startup 填充 / UART / 兼容表  
- 必要时本 Arch HAL 的入口约定小改（保存 `x0=DTB` 等）——尽量薄  
- U-Boot **环境变量 / 脚本**说明（不改厂商树源码）

**不交 / 不改**

- `Common/`、`User/`、Gui / FAT / VFS 业务  
- 厂商 U-Boot / BootROM / DRAM 初始化源码  
- 把 U-Boot 头或 DTB 解析引进 Common  
- 与 **1.3c**（x86 UEFI 桌面 PC）混在同一 PR  
- 在 Duo S 等命令行靶上做 GUI（GUI 仍 QEMU virt；桌面真机见 H 系列）

验收 diff 经验法则：`git diff --stat` 里 **Common 应为空**（能力旗标 **B1** 除外，且 B1 是跨板通用 HAL 门面，不是某一板包）。

---

## 4. U-Boot 验收口径（命令行板）

最小里程碑（对齐 **B3** / Duo S）：

1. 厂商 U-Boot（不改源码）能 `load` + `booti`（或等价）加载内核 + DTB。  
2. 串口出现 `Blocks ready`（或本板 README 写明的等价横幅）。  
3. `BOOT_HANDOFF.Regions` 与 DRAM 大致一致（来自 DTB `/memory` 或板级表）。  
4. **本柱跳过** video / gui / SD→FAT / 板上网卡（后置）。

概念命令（板 README 必须改成该机真实变量）：

```text
load mmc 0:1 ${kernel_addr_r} Image
load mmc 0:1 ${fdt_addr_r} board.dtb
booti ${kernel_addr_r} - ${fdt_addr_r}
```

合流点永远是 `BOOT_HANDOFF` → `KernelMain`（见 [`架构分层.md`](../../Archive/技术手册.md) §3）。

---

## 5. 与后续 PR

| PR | 本约定如何用 |
|----|----------------|
| **B0**（约定） | 清单 + `_template`；概念文互指 |
| **B1 ✅** | Common 消费 `HalHasFrameBuffer` / `HalConsoleOnly`，板包勾选能力 |
| **B2 ✅** | `BOARD=` 选中 `HAL/<Arch>/Board/<board>`；默认已收 `virt` |
| **B3 ✅** | Duo S：`HAL/RiscV/Board/milk-v-duo-s/`；厂商 U-Boot + SoC UART hello |

手机 SoC **明确不做**（不进 B/H）。
