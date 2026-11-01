# 26 · Pi 3B 蓝牙 UART 与固件清单（bt-pi-0 眼睛交付）

> 交付对象：[`Docs/24`](24-板端蓝牙-Pi与Duo执行包.md) § bt-pi-0。
> 性质：**只读调查结论**，不含行为代码；下一刀 `bt-pi-1` 才动驱动。
> 引用一律给到文件/行或上游 dts，**无一处靠猜**。

---

## 1 · 五问五答（§2.1 必填表）

| # | 问题 | 结论 | 证据 |
|---|---|---|---|
| 1 | 壳用哪根 UART？基址？ | **mini-UART（UART1/AUX）** @ `0x3F215000`，GPIO14/15 的 **ALT5**，115200 8N1 | `HAL/Arm64/Board/rpi-3b/BoardConfig.h:22-25`（`BOARD_UART_MINI=1`、`BOARD_UART_BASE=0x3F215000`）；`NOTES.md:8-9`；上游 dts `chosen { stdout-path = "serial1:115200n8"; }` |
| 2 | HCI 用哪根？基址？GPIO？ | **PL011（UART0）** @ `0x3F201000`，**GPIO32 = TXD0 / GPIO33 = RXD0（均 ALT3）** | `bcm2837-rpi-3-b.dts:131-135`：`/* uart0 communicates with the BT module */ &uart0 { pinctrl-0 = <&uart0_gpio32 &gpclk2_gpio43>; }`；`bcm283x-rpi-wifi-bt.dtsi`：`&uart0 { bt: bluetooth { compatible="brcm,bcm43438-bt"; max-speed=<2000000>; } }` |
| 3 | `BT_REG_ON` 脚号与极性？ | **不是 SoC GPIO**：由 **VideoCore 固件**经 expgpio 0 驱动（`GPIO_ACTIVE_HIGH`）。内核**不要**去拉；上电默认已开 | `bcm2837-rpi-3-b.dts:20-22`：`&bt { shutdown-gpios = <&expgpio 0 GPIO_ACTIVE_HIGH>; }`；`&firmware { expgpio: gpio {...} }` |
| 4 | `.hcd` 落盘路径与加载 API？ | `Assets/Firmware/brcm/BCM43430A1.hcd`（沿用 WiFi 的 `Assets/Firmware/brcm/` 约定）；读取用 `HalDriverFileRead(Path, Buf, Max, &Size)` | 路径约定：`HAL/Arm64/Board/rpi-3b/Wifi/Private.h:138-146`（`Assets/Firmware/brcm/brcmfmac43430-sdio.bin` 等）；API：`Include/Hal/HalDriverFile.h:16` |
| 5 | 与 WiFi SDIO 并存注意？ | 同模组、**不同总线不同引脚**：WiFi = SDHCI（GPIO34-39 一类），BT = UART0（GPIO32/33）；**电源都归固件**。禁止"为了试 BT 去关 WiFi 时钟/复位" | 同上 dts：`&sdhci { pinctrl-0 = <&emmc_gpio34>; ... brcmf: wifi@1 {...} }` 与 `&uart0 { bt: ... }` 并列 |

### 1.1 这条结论推翻了此前的假设

早前判断是"BT 与壳抢 GPIO14/15"。**上游 dts 明确否证**：

```
&uart0（BT）   → uart0_gpio32：GPIO32/33
&uart1（壳）   → uart1_gpio14：GPIO14/15，且 chosen stdout-path = serial1
```

⇒ **壳与 BT 可以同时存在**，不需要 `disable-bt`，也不需要把壳改回 PL011。
`NOTES.md:12` 里"若改走 PL011 须 disable-bt"是**改壳**的前提，与本柱无关。

---

## 2 · 引脚与复用总表（Pi 3B）

| 用途 | 控制器 | 基址 | 引脚 | 复用值 |
|---|---|---|---|---|
| 壳 console | UART1（AUX mini-UART） | `0x3F215000`（AUX；寄存器窗 `+0x40`） | GPIO14 TXD1 / GPIO15 RXD1 | ALT5 |
| **BT HCI** | **UART0（PL011）** | **`0x3F201000`** | **GPIO32 TXD0 / GPIO33 RXD0** | **ALT3** |
| WiFi | SDHCI | SDHCI 寄存器 | GPIO34-39（`emmc_gpio34`） | ALT3 |
| BT 电源 | VideoCore expgpio 0 | 经 mailbox/固件 | 不可见 | — |

**GPFSEL 位（写驱动时用）**：pins 30-39 在 **GPFSEL3**；pin32 = bits[8:6]、pin33 = bits[11:9]，ALT3 = `0b111`。
（BCM2835 ARM Peripherals §6.1 GPFSELn 表。）

---

## 3 · `.hcd` 固件：来源、落盘、加载序

### 3.1 来源
- Linux 发行版：`/lib/firmware/brcm/BCM43430A1.hcd`（Pi 3B 的 BCM43430 **A1** 版本，勿用 A0，理由同 WiFi：错版本"能跑但不通"）。
- 上游仓库：`raspberrypi/firmware` 的 `brcmfmac43430-sdio.bin` 同族目录下有对应 `BCM43430A1.hcd`。

### 3.2 落盘
```
Assets/Firmware/brcm/BCM43430A1.hcd     ← 与 WiFi 固件同目录，便于一起拷
```
**校验**：文件应约 20-30 KB；**首 4 字节必须是 `4C FC 21 12` 之类小端长度 + 后续 HCI 命令**（hcd 格式：每块 `[u16 len][... payload]`，payload 内含 HCI 命令头）。
眼睛刀只要求：**文件能按路径读到、非空、长度合理**。

### 3.3 加载序（对表 Linux `hci_bcm.c` + `hci_h4`）
```
① 打开 UART0，115200 8N1，无流控
② 发 HCI_Reset(0x0C03) → 等 Command Complete(0x0E)，status=0
③ 逐块写 .hcd：HCI_Write_RAM / Download_Minidriver（hcd 内部带 opcode，不另拼）
④ （可选）切高速：HCI brcm 的 Update_Baud_Rate → 2 Mbps（dts max-speed=2000000）
⑤ HCI_Read_BD_ADDR(0x1009) → 非零地址
⑥ HCI_Write_Local_Name("Blocks")（对表 NUC btx-3 语义）
⑦ Ready = ②+⑤ 都成功
```
**判据串口串（本柱约定）**：`Boot: Bluetooth UART0 up` → `HCI Reset ok` → `HCI BD_ADDR=xx:xx:xx:xx:xx:xx` → `Bluetooth ready`。

---

## 4 · 现场待确认项（bt-pi-1 开工前逐条打勾）

| # | 待确认 | 怎么确认 | 失败时的对策 |
|---|---|---|---|
| 1 | `.hcd` 文件确实存在且版本对 | 读 `Assets/Firmware/brcm/BCM43430A1.hcd`，长度 > 4 KB | 换 A1 版本；勿用 A0 |
| 2 | GPIO32/33 复用写 ALT3 后**不影响**壳 | 改复用前后各打一次壳串口 | 立即回滚；说明有别的占用 |
| 3 | BT 已上电（固件已给 expgpio 0） | HCI Reset 有 CC 即已上电 | 没有 CC → 检查固件配置；**不要**去拉 SoC GPIO |
| 4 | PL011 未被 U-Boot/固件占用 | 读 UART0 的 FR/CR 初值、试发 0x01 后回读 | 若被占用，先用 `fastboot`/`config.txt` 关掉固件串口 |
| 5 | 波特率 | 先 115200；Reset 成功后再切 2 Mbps | 切速失败就留在 115200（功能不受影响） |

---

## 5 · 拟新建文件（bt-pi-1/2 用，均 ≤300 行）

| 文件 | 职责 |
|---|---|
| `HAL/Arm64/Board/rpi-3b/Bluetooth/UartTransport.c` | PL011 收发 + GPIO32/33 ALT3 + 波特率 |
| `HAL/Arm64/Board/rpi-3b/Bluetooth/FirmwareLoad.c` | `.hcd` 分块 → HCI 命令（`HalDriverFileRead`） |
| `HAL/Arm64/Board/rpi-3b/Bluetooth/HciCommand.c` | 发命令 / 等 Command Complete / 错误码 |
| `HAL/Arm64/Board/rpi-3b/Bluetooth/BringUp.c` | Reset → 固件 → BD_ADDR → 名字 → Ready |

（不改 `Wifi/**`；不改壳串口配置。）

---

## 6 · 手测口令（bt-pi-2 验收时用）

```
① 串口接 Pi（USB-TTL 在 GPIO14/15，115200）
② 部署新 Kernel.elf（CK 或 ksend）→ 冷启
③ 期望串口出现：
   Boot: Bluetooth UART0 up
   HCI Reset ok
   HCI BD_ADDR=<非零 MAC>
   Bluetooth ready
④ 壳命令 bt → 打印 BD_ADDR + 名（Blocks）
⑤ 回归：壳串口依旧可用（mini-UART 未被抢）
```
