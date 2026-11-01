# 24 · 板端蓝牙（Pi + Duo）— DeepSeek 执行包

> **给执行者（DeepSeek）**：按 ★ **一刀**做；一次一变量；未过判据 **不 TG / 不 push**。  
> **给评审（Cursor）**：对表 §8 红线；填本刀表末行 **评审**（与 Docs/25 同形）。  
> **背景**：NUC 课轨 **btx-1…5 已收官**（USB HCI）。Pi/Duo 门面 `HalBluetooth*` 仍 **Ready=0 stub**。  
> **权威锚**：门面 `Include/Hal/HalBluetooth.h`；NUC `HAL/X64/.../XHCI/Bluetooth/`；[`路线图1027`](../Archive/路线图1027.md) §bt-pi/duo；[`1030`](../Archive/项目/进展/路线图1030.md) §7.5。  
> **前置柱**：[`Docs/25`](25-DUO-WiFi-挂通用网执行包.md) Duo WiFi 挂网 — **已收官（2026-11-02）**。  
> **当前执行包（已切柱）**：本文 Docs/24；★=`bt-duo-2`。详见 [`TO-DS.md`](../Archive/协作信箱/TO-DS.md)。

### 协作闭环（与 Docs/25 同形）

| 谁 | 动作 |
|---|---|
| **DeepSeek** | 填「进度」→ 等「评审」**`✅ 全绿`** → 才 TG+TS → JX；每 ~5 分钟读 [`TO-DS.md`](../Archive/协作信箱/TO-DS.md)；**评审读到段尾「给 DS」** |
| **Cursor** | 进度更新 → 填「评审」；协作/文档变更写进评审 **「给 DS」**；切柱时写 TO-DS |
| **开刀条件** | 仅当 TO-DS / 1101 ★ 写明 **当前执行包=Docs/24**（通常在 Docs/25 收官后） |
| **第一刀** | `bt-pi-0`（眼睛 → 交付 Docs/26）；表须有 **进度** + **评审** 行 |

> **切换时**：Cursor 会在 TO-DS 写「离开 Docs/25 → 进入 Docs/24 ★=bt-pi-0」；DS 见后停止改 Wifi/netif，改读本文。

---

## 0 · 目标与非目标

| 要 | 不要 |
|---|---|
| 三架构 **同形** `HalBluetooth*`：`Initialize` / `Ready` / `GetDeviceAddress` / `GetLocalName`；（可选后刀）`Discoverable` / `Scan` 与 NUC 同语义 | 空 stub 却 `Ready=1`；假 BD_ADDR |
| Pi：**UART HCI** + 厂商 `.hcd` → HCI Reset → 读 BD_ADDR → Ready；壳 `bt` 打出地址/名 | 抢走现网 **壳用 mini-UART**；未钉 UART 分工就写驱动 |
| Duo：先 **探针有无 BT 通路**；有则 HS-UART + AIC BT 固件 → 同形 Ready；无则串口明确 `BT: skip (no …)` 且 Ready=0 | 改 `Sdhci/Wifi*.c`「顺便」；为 BT 重开 dwifi / 碰 Docs/25 netif |
| 命名写全、单文件 ≤300（新写/大改自有 `.c/.h`） | `Hc`/`Cfg`/`Init`/`Tx`/`Rx`/`Reg` 等禁缩写顶全称 |
| **标准 / 厂商驱动 / 本仓 NUC 行为** 三选一作主参考 | 猜波特率狂扫、启发式狂重试、瞎缩超时 |

**本包最小闭环（对齐 NUC btx-3，不强求 btx-4/5）**：

1. `HalBluetoothReady() == 1`  
2. 壳 `bt`（无参 = info）打出 **非零 BD_ADDR** + 本地名（建议仍 **`Blocks`**）  
3. 失败路径可诊断、不卡死壳；冷启可重复  

**加分（本包默认不做，另开刀）**：`bt scan` / `bt visible`（NUC btx-4/5）；手机搜到名。

---

## 1 · 为何现在不通（复现）

```text
Blocks> bt
bt: not ready (no HCI / Reset / BD_ADDR yet)
```

原因（对表代码，2026-11-01）：

| 架构 | 文件 | 现状 |
|---|---|---|
| Arm64 (Pi) | `HAL/Arm64/Hal/HalBluetooth.c` | stub：`Ready=0`；注释写明「Pi UART HCI 在 bt-pi」 |
| RiscV (Duo) | `HAL/RiscV/Hal/HalBluetooth.c` | stub：`Ready=0`；「Duo 路径在 bt-duo」 |
| X64 (NUC) | `HAL/X64/…/XHCI/Bluetooth/*` | ✅ btx-1…5 已绿 |

壳已通用：`Services/Shell/Commands/Bluetooth.c` 注册 `bt [info|scan|visible|hide]` — **板侧 Ready 起来即可演示，一般不必改壳**。

---

## 2 · 硬件与串口边界（动手前必读）

### 2.1 Pi 3B（BCM43430 / CYW43438 组合芯）

| 项 | 钉死口径（以板注释为准） |
|---|---|
| 总线 | **UART HCI**（非 USB；勿抄 NUC xHCI 端点） |
| 固件 | `BCM43430A1.hcd`（Linux 常见 `/lib/firmware/brcm/`）；本仓 **未必已入库** — bt-pi-0 须写清如何进 `BLOCKS:` / `Assets/Firmware/…` |
| 电源 | dts：`BT_REG_ON` 等 GPIO；对表 Pi dts / WiFi `WL_REG_ON` 经验，**禁止瞎猜脚号** |
| **壳串口（现网）** | `BoardConfig.h`：`BOARD_UART_MINI=1`，`BOARD_UART_BASE=0x3F215000`（**AUX mini-UART**，GPIO14/15）。证据：无 `disable-bt` 时 U-Boot 有字；加 overlay 则整卡无提示。见 `rpi-3b/NOTES.md` / `BoardConfig.h` 注释 |
| **HCI 串口（预期）** | 经典路径：**PL011 UART0** @ `0x3F201000`。**禁止**在未论证时把壳改回 PL011，也禁止 BT 占用 mini-UART 把壳弄哑 |
| 对照驱动 | Linux `hci_uart` + `hci_bcm.c`（下发 `.hcd` 流程）；HCI **命令语义**对表本仓 NUC `Bluetooth/Hci.c` + `Private.h` |
| 部署 | 真机 **CK**（`ck-pi-sd`）或现网 `ksend`/`deploy-pi`；口令见协作暗号 |

**bt-pi-0 必须先填的表（写入 Docs/26，见下）**：

| # | 问题 | 必须有答案 |
|---|---|---|
| 1 | 壳用哪根 UART？基址？ | 现网：mini-UART `0x3F215000` |
| 2 | HCI 用哪根？基址？GPIO？ | 写死或否证「须改 dts」 |
| 3 | `BT_REG_ON` 脚号与极性？ | 对表 dts，附引用 |
| 4 | `.hcd` 落盘路径与加载 API？ | 对表 WiFi `HalDriverFileRead` / `Assets/Firmware/…` |
| 5 | 与 WiFi SDIO 并存注意？ | 同模组；电源序；勿关 WiFi 时钟「试 BT」 |

### 2.2 Duo S（AIC8800D80）

| 项 | 钉死口径 |
|---|---|
| 总线 | SDK：**HS-UART** + `aic8800_btlpm`；**非** SDIO WiFi 数据通路 |
| BT 固件 | SDK 已有例：`Source/DUO/sdk/…/firmware/aic8800/fmacfwbt_8800d80_u02.bin`（路径以树内实际为准；bt-duo-0 抄到清单） |
| 与 WiFi | 同模组不同核。`WifiPrivate.h` 里 `AIC_BT_*` / `AICBT_PT_*` 是 **WiFi 侧 patch/coex 表**，**不是** HCI bring-up API — **禁止**在 `WifiPatchTable.c` 里「改 btmode 当蓝牙驱动」 |
| 刀 0 | 只探针：时钟/GPIO/UART 是否出字、固件是否可读；无通路 → **正式 skip**，Ready 保持 0 |
| 部署 | Duo：**只**拷 `Kernel.elf`→LABEL=`boot`；**禁止**对 Duo 跑 `ck-pi-sd` |
| WiFi 柱 | Docs/25 `dwifi-netif-*` 进行中时：**BT 会话勿改** `Sdhci/Wifi*` / `LwIp/Netif.c` / `HalDevicesNet.c` |

### 2.3 NUC 对表（只抄契约，不抄 USB）

| 抄 | 不抄 |
|---|---|
| `HalBluetooth*` 返回值；Ready 条件 = **Reset 成功 + Read_BD_ADDR 成功** | xHCI Bulk/Intr / ConfigureSlot 整段 |
| 本地名 **`Blocks`**；壳 `bt` 展示格式（`Bluetooth.c`） | bt-defer / Worker Reveal（Pi/Duo **console-only** 无桌面；Init 时机另钉，勿照搬 NUC Reveal） |
| HCI 操作码与完成事件（见 §3） | Inquiry / 可发现（btx-4/5，本包后排） |
| 串口日志语气：`Boot: Bluetooth …` / `HCI Reset ok` | USB 类码 `E0/01/01` 枚举逻辑 |

---

## 3 · HCI 标准钉（禁止猜）

> Core Spec：Command packet；Event `Command Complete (0x0E)`。操作码与本仓 NUC `Private.h` 对齐，便于评审抽查。

| 用途 | Opcode | 本包刀 |
|---|---|---|
| HCI_Reset | `0x0C03` | pi-1 / duo-1 |
| Write_Local_Name | `0x0C13` | pi-2 / duo-1 |
| Write_Scan_Enable | `0x0C1A` | **后排**（btx-5） |
| Read_BD_ADDR | `0x1009` | pi-2 / duo-1 |
| Inquiry | `0x0401` | **后排**（btx-4） |

事件：

| 事件 | Code | 用法 |
|---|---|---|
| Command Complete | `0x0E` | Reset / Read_BD_ADDR / Write_Local_Name 完成 |
| Command Status | `0x0F` | 异步命令；本包前三刀尽量走 Complete |

**UART 绑运（Pi / Duo 共性，细节以厂商为准）**：

- HCI over UART：常见 H4（指示字节 + HCI 包）。**波特率 / 流控以厂商文档或 `hci_bcm` / AIC btlpm 为准**，禁止从 115200 扫到 3M「碰运气」。  
- Duo SDK 提示值（仅作清单候选，bt-duo-0 须对表源码再钉）：`WifiPrivate.h` 注释侧曾录 `AIC_BT_BAUD=1500000`、`FLOWCTRL=1` — **那是 WiFi coex 表常量，不等于已接好 HS-UART**；真正波特率以 `aic8800_btlpm` / 板级 UART 驱动为准。

**Ready 语义（铁律，与 `HalBluetooth.h` 注释一致）**：

```text
Ready == 1  ⇔  HCI Reset 成功 且  Read_BD_ADDR 成功（地址非全 0 为佳）
Initialize 成功 ≠ Ready（门面可挂、控制器未起）
未 Ready → GetDeviceAddress / GetLocalName / Scan / SetDiscoverable 均失败（-1 或 0）
```

---

## 4 · 架构：NUC 已绿 vs Pi/Duo 缺口

```text
                    ┌──────── 壳 bt ────────┐
                    │ info / scan / visible │
                    └──────────┬────────────┘
                               │
                        HalBluetooth*
                               │
          ┌────────────────────┼────────────────────┐
          │                    │                    │
     X64 NUC ✅            Arm64 Pi              RiscV Duo
   USB HCI (xHCI)         UART HCI 缺口         HS-UART 缺口
   Reset→BD_ADDR→Ready    stub Ready=0          stub Ready=0
```

| 层 | NUC（已绿） | Pi（要做） | Duo（要做） |
|---|---|---|---|
| 传输 | xHCI 控制+中断 | PL011（预期）H4 UART | HS-UART + 厂商 btlpm |
| 固件 | 控制器内置 / USB | `.hcd` 下发（`hci_bcm` 序） | `fmacfwbt_*.bin` |
| 门面 | `HAL/X64/Hal/HalBluetooth.c` → Board | 改 `HAL/Arm64/Hal/HalBluetooth.c` 接 Board | 改 `HAL/RiscV/Hal/HalBluetooth.c` |
| 壳 | `Bluetooth.c` | **尽量不改** | **尽量不改** |

---

## 5 · 必读文件清单

### 5.1 NUC 对表（只读，禁止改）

| 文件 | 看什么 |
|---|---|
| `Include/Hal/HalBluetooth.h` | API 契约与 Ready 注释 |
| `HAL/X64/Drivers/Bus/XHCI/Bluetooth/Private.h` | opcode / event 常量 |
| `…/Bluetooth/Hci.c` | 发命令、等 Complete |
| `…/Bluetooth/Info.c` | Reset 后 Read_BD_ADDR + Write_Local_Name(`Blocks`) + Ready |
| `…/Bluetooth/Visible.c` / `Scan.c` | **后排**才读；本包前三刀勿实现 |
| `Services/Shell/Commands/Bluetooth.c` | 壳输出格式 |

### 5.2 Pi 战场（bt-pi-* 可改）

| 文件 | 看什么 / 预期 |
|---|---|
| `HAL/Arm64/Board/rpi-3b/BoardConfig.h` | **壳** mini-UART 钉死；HCI 另口 |
| `HAL/Arm64/Board/rpi-3b/NOTES.md` | 串口证据；改 PL011 的前提 |
| `HAL/Arm64/Hal/HalBluetooth.c` | 现 stub；pi-1/2 接线 |
| **新建** `HAL/Arm64/Board/rpi-3b/Bluetooth/*.c` | 建议拆：`UartTransport` / `FirmwareLoad` / `HciCommand` / `BringUp`（均 ≤300、全称命名） |
| WiFi 目录 | **默认只读**；仅允许 bt-pi-0 清单引用电源/固件路径经验 |

### 5.3 Duo 战场（bt-duo-* 可改）

| 文件 | 看什么 / 预期 |
|---|---|
| `HAL/RiscV/Hal/HalBluetooth.c` | stub → 接线 |
| `…/Sdhci/WifiPrivate.h` | **只读参考** coex/波特率注释；勿当 BT 驱动入口 |
| SDK `fmacfwbt_8800d80_u02.bin` + `aic8800_btlpm` | bt-duo-0 对表抄基址/GPIO/加载序 |
| **新建** `HAL/RiscV/Board/milk-v-duo-s/Bluetooth/*.c`（或板级等价树） | 与 WiFi `Sdhci/` **分树** |
| `…/Sdhci/Wifi*.c` | **禁止改**（除非用户点名且与 Docs/25 ★ 无关） |

### 5.4 禁止碰（本柱）

| 路径 | 原因 |
|---|---|
| `HAL/X64/Drivers/Bus/XHCI/Bluetooth/**` | NUC 已绿课轨 |
| `HAL/Arm64/Board/rpi-3b/Wifi/**` | WiFi 隔离 |
| `HAL/RiscV/Board/milk-v-duo-s/Sdhci/Wifi*` | Duo WiFi / Docs/25 |
| `HAL/RiscV/LwIp/**`、`HalDevicesNet.c` | netif 柱 |
| 抽公共「UART-HCI 大框架」跨 Pi/Duo | **两板都 Ready 后再议** |

---

## 6 · 对表表（bt-pi-0 / bt-duo-0 眼睛交付）

> **bt-pi-0**：把下表「Pi 结论」列填满，并落盘 [`Docs/26-PI-蓝牙UART与固件清单.md`](26-PI-蓝牙UART与固件清单.md)（**勿再写 Docs/25** — 25 已是 Duo WiFi 挂网包）。  
> **bt-duo-0**：填「Duo 结论」；可写短节进 Docs/26 附录或 `Docs/27-DUO-蓝牙通路探针.md`。

| # | 问题 | NUC / 标准锚 | Pi 结论（pi-0 已填） | Duo 结论（duo-0 ✅ [`Docs/27`](27-DUO-蓝牙通路探针.md)） |
|---|---|---|---|---|
| 1 | 壳用哪根串口？ | — | **UART1 mini-UART** @`0x3F215000`，GPIO14/15 ALT5，115200（`BoardConfig.h:22-25`+`NOTES.md:8-9`+dts `stdout-path=serial1`） | **UART0** @`0x04140000`，DW-APB，`reg-shift=2`，115200（`BoardConfig.h:13-16`） |
| 2 | HCI 用哪根 UART？基址？ | H4 over UART | **PL011 UART0** @`0x3F201000`，**GPIO32 TXD0 / GPIO33 RXD0（ALT3）**；dts `&uart0 { pinctrl-0=<&uart0_gpio32 …> }` | **`BT: duo path = UART4@0x041C0000`**（DW-APB）；U-Boot 将 UART2 焊盘 mux→UART4（含 CTS/RTS）；`cv181x_base.dtsi:293` + `cvi_board_init.c:44-49` |
| 3 | 电源 / 复位 GPIO？ | — | **非 SoC GPIO**：`&bt { shutdown-gpios=<&expgpio 0 GPIO_ACTIVE_HIGH> }` → 由 VideoCore 固件驱动，内核勿拉 | 模组上电 **GPIOA15**（与 WiFi 共用，`&wifi_pin`）；wakeup **PORTE7**（btlpm） |
| 4 | 固件文件与加载序？ | — | `Assets/Firmware/brcm/BCM43430A1.hcd`；`HalDriverFileRead`（`Include/Hal/HalDriverFile.h:16`）；序：Reset→逐块写→(2 Mbps)→Read_BD_ADDR→Write_Local_Name | SDK `…/firmware/aic8800/fmacfwbt_8800d80_u02.bin`（SDIO 灌）；HCI 走 UART H4 @1.5M+流控（`AIC_BT_*`）；**btlpm≠HCI** |
| 5 | Reset 成功判据串口串？ | `HCI Reset ok` / CC `0x0C03` | `Boot: Bluetooth UART0 up` → `HCI Reset ok` → `HCI BD_ADDR=<非零>` → `Bluetooth ready` | 拟：`Boot: Bluetooth UART4 up` → `HCI Reset ok` → `HCI BD_ADDR=…` → `Bluetooth ready` |
| 6 | Ready 条件？ | Reset+BD_ADDR | 同形：Reset 的 CC status=0 **且** Read_BD_ADDR 非零 | **同形**（本刀 Ready 仍 0；非 skip） |
| 7 | Init 时机？ | NUC bt-defer Reveal 后 | console-only：**启动末一次**（或壳 `bt init` 显式触发），**不挡进壳** | 同左 |
| 8 | 与 WiFi 冲突面？ | — | **无引脚冲突**：WiFi=SDHCI(GPIO34-39)、BT=UART0(GPIO32/33)，电源均归固件；禁止为试 BT 关 WiFi 时钟 | SDIO≠UART4；电源共享 A15；**禁止改 `Wifi*`**；`AIC_BT_*` 仅 coex/patch |
| 9 | 拟新建文件名（全称、≤300）？ | — | `Bluetooth/UartTransport.c`、`FirmwareLoad.c`、`HciCommand.c`、`BringUp.c` | 同形：`milk-v-duo-s/Bluetooth/{UartTransport,FirmwareLoad,HciCommand,BringUp}.c`（与 `Sdhci/` 分树） |
| 10 | 手测口令？ | — | 串口接 GPIO14/15 → 部署 Kernel.elf（CK/ksend）→ 冷启看四条串；壳 `bt` 出 MAC+名；回归壳串口可用 | 串口仍 UART0；elf→LABEL=`boot`（禁 `ck-pi-sd`）；duo-1 后再验 HCI |

**眼睛刀判据**：表无空结论（或 Duo 整列写「本刀未开」）；**无行为代码**（或仅 `BT: eyes ok` 日志）；评审能按表找到符号/基址引用。

---

## 7 · 刀序详表

> 协作：活档文首 ★ 是哪刀就只做哪刀。改序须在本节与日档同步。  
> **推荐序：先 Pi 再 Duo**。允许 **bt-duo-0 文档探针** 与 **bt-pi-1** 分会话并行；**禁止**同一会话同时大改两板代码。  
> **与 Docs/25**：netif ★ 占用时，本柱 ★ 勿与用户抢同一执行会话改 Duo。

### bt-pi-0 · 串口与固件钉死（眼睛）

| 项 | 内容 |
|---|---|
| **做** | 精读 §2.1 / §5；填 §6 Pi 列；新建 **`Docs/26-PI-蓝牙UART与固件清单.md`**（壳 UART / HCI UART / `BT_REG_ON` / `.hcd` 落盘 / WiFi 并存） |
| **判据** | Docs/26 有可勾表；§6 Pi 列无空；无 Ready 谎言 |
| **禁止** | 改 `HalBluetooth.c` 声称 Ready；改 WiFi；改壳占口 |
| **进度** | ✅ **收官（2026-11-02 00:26，commit `cbed21d4`，已 push）**<br>产出 `Docs/26-PI-蓝牙UART与固件清单.md`；§6 Pi 列 10 行填满。关键否证：上游 dts 证明 BT=UART0/PL011 `0x3F201000` GPIO32/33 ALT3、壳=UART1 mini-UART `0x3F215000` GPIO14/15 ⇒ **不抢口**；`BT_REG_ON` 归 VideoCore expgpio 0。评审 ✅ 全绿（00:20 Cursor）。<br>**另**：`.hcd` 已入库 `Runtime/Assets/Firmware/brcm/BCM43430A1.hcd`（30,049 B，A1；来源 Ubuntu `bluez-firmware` 包，头 `4c fc 46 10`）。 |
| **评审** | ✅ **全绿**（2026-11-02 00:20 Cursor）。抽查：`BoardConfig.h` mini-UART/`0x3F215000` 对齐；`HalDriverFileRead`/`Assets/Firmware/brcm/` 约定对齐；上游 `bcm2837-rpi-3-b.dts` 确认 **uart0→GPIO32/33 + expgpio0=BT_ON**，与 Docs/26 一致——**同意「壳与 BT 不抢口」**。无行为代码、无假 Ready。§6 Duo 列仍是提示语，眼睛刀允许后置，不挡。小债：文内「眼睛刀判据」重复字已清；仓内尚无 `*.hcd`（不挡本刀）。**给 DS**：允许 **`TG`+`TS`**（Docs/26+Docs/24 进度/§6；勿塞无关）。再 **`JX bt-pi-1`**。**.hcd**：是，下一刀开工前把 `BCM43430A1.hcd` 放进 `Assets/Firmware/brcm/`（可从本机 `/lib/firmware/brcm/` 拷，勿用 A0）；四文件名按 Docs/26 §5。GPIO32/33/上电三项留在 pi-1 真机勾。 |

### bt-pi-1 · UART 字节通路 + HCI Reset

| 项 | 内容 |
|---|---|
| **做** | Board `Bluetooth/`：开电 → 发 **HCI_Reset (`0x0C03`)** → 等 Command Complete；串口一行成功/失败。**`.hcd` 加载不在本刀**（按 Cursor 00:40「出主意·选 1」收窄，挪到 `bt-pi-2`；本刀不建 `FirmwareLoad.c`） |
| **判据** | 串口可 grep：`BT: HCI Reset ok`（或同形）；失败可诊断、**不卡死壳**；壳仍走 mini-UART |
| **战场** | `HAL/Arm64/Board/rpi-3b/Bluetooth/**`；`HAL/Arm64/Hal/HalBluetooth.c` 可开始接 Initialize（Ready 仍须 0，直到 pi-2） |
| **禁止** | 动 Duo；动 NUC XHCI BT；动 WiFi；扫波特率 |
| **依赖** | bt-pi-0 |
| **手测** | Pi 3B；**CK** 或 ksend；TTL 接壳口 |
| **进度** | ✅ **收官（2026-11-02 01:18 Cursor 接手）**<br>根因：PL011 钟是 **mailbox UART=48 MHz**，不是 mini-UART 的 `core_freq=250`；旧 IBRD/FBRD=135/40 把 HCI 波特率打飞。改为 `BoardMboxGetClockHz(UART)` → **IBRD=0x1a FBRD=0x03**。<br>另：GPCLK2 keep + BT_ON=128 脉冲 + BootMark。<br>真机（ksend `@ttyUSB1`）：`bt: diag=HCI Reset ok clk=02dc6c00 ibrd/fbrd=0000001a/00000003`；`log` → `Boot: Bluetooth HCI Reset ok`。**Ready 仍 0**（pi-2）。无 `.hcd`。 |
| **评审** | ✅ **全绿**（2026-11-02 01:18）。判据 `HCI Reset ok` 齐；壳 mini-UART 未抢；Ready=0。**给用户/DS**：可 **`TG`+`TS`** → ★ **`bt-pi-2`**（`.hcd`+BD_ADDR+Ready）。 |

### bt-pi-2 · 下发 .hcd + BD_ADDR + 名 + Ready

| 项 | 内容 |
|---|---|
| **做** | 对表 Docs/26 §3.3：Reset 后 **逐块下发 `BCM43430A1.hcd`** → `Read_BD_ADDR (0x1009)` → `Write_Local_Name (0x0C13)`=`Blocks`；`Ready=1` **当且仅当** Reset+读址成功；壳 `bt` 同 NUC 信息量 |
| **判据** | `bt: ready name=Blocks addr=XX:…`（非全 0）；冷启可重复；未成功时 `bt: not ready …`；串口可 grep `HCI BD_ADDR=` / `Bluetooth ready` |
| **禁止** | Ready=1 但地址全 0；实现 scan/visible「顺便」；扫波特率 |
| **依赖** | bt-pi-1 |
| **手测** | 同上；收口贴串口原文 |
| **进度** | ✅ **收官（2026-11-02 01:31 Cursor）**<br>实现：`FirmwareLoad.c`（`0xFC2E`+逐块 `.hcd`，对表 `btbcm_patchram`）→ 再 Reset → `Read_BD_ADDR` → `Write_Local_Name(Blocks)` → Ready。<br>真机 ksend `@ttyUSB1`：`bt: diag=ready`；`bt: ready name=Blocks addr=B8:27:EB:E8:99:11 hidden`（Pi OUI）。未做 scan/visible。 |

### bt-duo-0 · 有无 BT（探针）

| 项 | 内容 |
|---|---|
| **做** | 对照 SDK：HS-UART 基址、复位 GPIO、`fmacfwbt` 路径、btlpm 加载序；串口打印「见/不见」证据；填 §6 Duo 列 |
| **判据** | 二选一写死：`BT: duo path = UART@…` **或** `BT: skip (no HS-UART/fw)` + 否证段 |
| **禁止** | skip 时 Ready=1；改 `Wifi*` / netif；对 Duo 跑 `ck-pi-sd` |
| **依赖** | 建议 bt-pi-2 绿后；若抢时间仅允许**文档探针**与 pi-1 并行 |
| **进度** | ✅ **收官（2026-11-02 01:40 Cursor JX）**<br>产出 [`Docs/27-DUO-蓝牙通路探针.md`](27-DUO-蓝牙通路探针.md)；§6 Duo 列填满。<br>**判据**：**`BT: duo path = UART4@0x041C0000`**（非 skip）。证据：`cv181x_base.dtsi` uart4；`cvi_board_init.c` WIFI/BT pinmux UART2→UART4；SDK `fmacfwbt_8800d80_u02.bin`；`aic8800_btlpm`=LPM 非 HCI。无行为代码、Ready 仍 0。 |
| **评审** | ✅ **全绿**（2026-11-02 Cursor）。判据串写死；§6 Duo 列可对表 dtsi/pinmux/SDK 文件；无 Ready 谎言、未碰 `Wifi*`。 |

### bt-duo-1 ·（仅 duo-0 有通路）Reset → 址 → Ready

| 项 | 内容 |
|---|---|
| **做** | 对齐 bt-pi-1/2 **语义**；总线换 AIC 厂商流程；`HalBluetooth*` 同形 |
| **判据** | 同 bt-pi-2；或文档否证「固件/UART 不通」并 **Ready=0** |
| **禁止** | 半截 stub Ready；碰 WiFi 数据面 |
| **进度** | ✅ **收官·否证（2026-11-02 02:10 Cursor）**<br>实现：`Bluetooth/{UartTransport,HciCommand,BringUp}.c` + `HalBluetooth` + `BOARD_HAS_BLUETOOTH`；ConsoleOnly `VirtMin` 补挂 `bt`；`bt init` 打 `diag=`。<br>**未灌 `fmacfwbt`、未改 `Wifi*`**（BringUp 头注释钉死）。真机 `@ttyUSB0`（ksend `BOOT:boot.sd`）：`bt: diag=HCI Reset fail (fw/UART?)`；`init rc=0`；`bt: not ready`；**Ready=0**。<br>**判据走第二条**：文档否证「现网 `fmacfw` 下 HCI/UART 不通」+ Ready=0（非 stub 假绿）。Ready=1 / BD_ADDR 需另刀灌 `fmacfwbt`。 |
| **评审** | ✅ **全绿（否证径）**（2026-11-02 Cursor）。串口原文齐；Ready 未撒谎；未碰 `Wifi*`/netif。**非** bt-pi-2 同形 Ready 绿。 |

### bt-duo-2 · 灌 `fmacfwbt` → Reset → 址 → Ready

| 项 | 内容 |
|---|---|
| **做** | SDIO 阶段选 `fmacfwbt_8800d80_u02.bin`（卡上 `/Assets/Fw/aic8800/` 已有）→ `write confirm` 同形管线 → UART4 HCI Reset → BD_ADDR → Ready；`Bluetooth/FirmwareLoad.c`；允许 **固件阶段选路**钩子，**禁止**改 WiFi 数据面 / netif / `btmode` 冒充 |
| **判据** | 同 bt-pi-2：`bt: diag=ready` + 非零 `addr=` + Ready=1；失败可诊断、Ready=0 |
| **禁止** | 半截 stub Ready；改 `Wifi*Data*` / Docs/25 netif；扫波特率 |
| **进度** | 🔧 ★ 当前 |
| **评审** | |

### 后排（本包明确不做）

- `bt scan` / Inquiry / RNR（NUC btx-4）  
- `bt visible` / Write_Scan_Enable / EIR（NUC btx-5）  
- BLE / GATT / A2DP / HID-over-BT  
- 抽公共 UART-HCI 框架跨 Pi/Duo  
- 为「看起来有蓝牙」改壳文案却不接 HCI  

---

## 8 · 禁止与评审红线

### 禁止

- 改 NUC `XHCI/Bluetooth/**`「参考实现」顺手重构  
- 改 Pi/Duo **WiFi** 目录或 Docs/25 netif 代码  
- 未钉串口分工就占用 **mini-UART**（壳）  
- 空实现 `Ready=1`；全 0 BD_ADDR 当成功  
- 猜波特率 / 狂重试 / 与注释对着干（Pi `BoardConfig` / NOTES）  
- 单文件 >300；禁缩写顶全称  
- 未口令 commit/push；Duo 误跑 `ck-pi-sd`  
- 同一 JX 会话同时改 BT + WiFi  

### Cursor 评审勾选

- [ ] 是否改了 Wifi / dhcp flags / netif？  
- [ ] Ready 是否在未读到合法 BD_ADDR 时仍为 1？  
- [ ] Pi 是否未论证就占用了壳用 mini-UART？  
- [ ] HCI opcode 是否对表 §3 / NUC `Private.h`（非臆造）？  
- [ ] 行为是否与 `HalBluetooth.h` 语义同形（仅总线不同）？  
- [ ] 命名 / 行数自查是否过？  
- [ ] 手测板与口令（CK / 拷 elf / TTL 接哪台）是否写清？  
- [ ] 眼睛刀是否交付 Docs/26（而非误写 Docs/25）？  

---

## 9 · 每刀收口交付

| 交付 | 说明 |
|---|---|
| 代码或文档 | 眼睛刀：Docs/26 + §6 表；代码刀：Board + Hal 接线 |
| 串口原文 | 可 grep：`HCI Reset ok` / `addr=` / `bt: ready` / `BT: skip` |
| 自查 | 命名写全；碰过的 `.c/.h` ≤300 |
| 否证 | 走不通写清变量与现象，**停**，勿连开三刀硬闯 |
| 报告 | 文件列表、是否要手测、下一刀名 |

---

## 10 · 开场口令

**眼睛（推荐第一刀）**：

```text
JX
只做 Docs/24 ★ = bt-pi-0
权威：Docs/24 §6 + 交付 Docs/26-PI-蓝牙UART与固件清单.md
勿改 WiFi / Docs/25 netif；勿改 NUC BT；不 commit/push
壳串口保持 mini-UART；HCI 口必须另钉
填完 §6 Pi 列后收口，等 PS
```

**Reset 刀**：

```text
JX
只做 Docs/24 ★ = bt-pi-1
权威：Docs/24 §7 bt-pi-1 + Docs/26 已钉 UART/固件
新建 Board/rpi-3b/Bluetooth/；HCI_Reset 0x0C03
禁止扫波特率；禁止动 Duo / WiFi
```

**Duo 探针**（Pi Ready 后或文档并行）：

```text
JX
只做 Docs/24 ★ = bt-duo-0
只读 SDK + 探针日志；禁止改 Sdhci/Wifi*
无通路则 BT: skip，Ready 保持 0
```

其后：手测过 → 用户 `TG` → ★ 推下一刀 → 再 `JX`。
