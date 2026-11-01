# 24 · 板端蓝牙（Pi + Duo）— DeepSeek 执行包

> **给执行者（DeepSeek）**：按刀序做；每刀一次一变量；未过判据 **不 TG**。  
> **给评审（Cursor/人）**：对表本节「禁止 / 判据 / 对标 NUC」；勿扩成 A2DP/BLE GATT。  
> **背景**：NUC 课轨 **btx-1…5 已收官**（USB HCI）。Pi/Duo 门面 `HalBluetooth*` 仍 **Ready=0 stub**。Duo WiFi（dhcp+ping）已绿。  
> **权威锚**：[`Archive/路线图1027.md`](../Archive/路线图1027.md) §bt-pi / §bt-duo；门面 `Include/Hal/HalBluetooth.h`；NUC 实现 `Kernel/HAL/X64/Drivers/Bus/XHCI/Bluetooth/`。

---

## 0 · 目标与非目标

| 要 | 不要 |
|---|---|
| 三架构 **同形** `HalBluetooth*`：Initialize / Ready / GetDeviceAddress / GetLocalName；（可选）Discoverable 与 NUC 同语义 | 空 stub 却 `Ready=1` |
| Pi：**UART HCI** + 厂商 `.hcd` → HCI Reset → 读 BD_ADDR → Ready；壳能打地址/名 | 抢走 CoolTerm 正在用的调试串口（无证据不许默默换） |
| Duo：先 **探针有无 BT 通路**；有则 HS-UART + AIC BT 固件 → 同上 Ready；无则串口明确 `BT: skip (no …)` | 把 WiFi SDIO 路径改坏；为 BT 重开 dwifi |
| 命名写全、单文件 ≤300（新写/大改自有 `.c/.h`） | `Hc`/`Cfg`/`Init` 等禁缩写顶全称 |
| 标准 / 厂商驱动 / 本仓 NUC 行为 三选一作主参考 | 启发式狂重试、瞎缩超时 |

**课堂最小闭环（对齐 NUC btx-3，不强求 btx-5）**：

1. `HalBluetoothReady()==1`  
2. 串口/壳打出 **BD_ADDR** + 本地名（建议仍 `Blocks`）  
3. （加分，可另刀）可被手机搜到名 — **非本包必达**

---

## 1 · 硬件与对照（动手前必读）

### 1.1 Pi 3B（BCM43430 / CYW43438 组合芯）

| 项 | 内容 |
|---|---|
| 总线 | **UART HCI**（非 USB） |
| 固件 | `BCM43430A1.hcd`（须落盘到板可读路径；Linux 常见 `/lib/firmware/brcm/`） |
| 电源 | dts：`BT_REG_ON` 等 GPIO — 对表现网 Pi dts / 已有 WiFi `WL_REG_ON` 经验 |
| 串口冲突 | 调试常用 **mini-UART**；BT 常占 **PL011 UART0**。**刀 0 必须先钉：哪根给壳、哪根给 HCI**，写进清单再写驱动 |
| 对照 | Linux `hci_bcm.c` / `hci_uart`；本仓 NUC 只对 **HCI 命令语义**，不对 USB 端点 |

### 1.2 Duo S（AIC8800D80）

| 项 | 内容 |
|---|---|
| 总线 | SDK：**HS-UART** + `aic8800_btlpm`；BT 固件 `fmacfwbt_*.bin`（SDK 已有，见 1030 §7.5） |
| 与 WiFi | 同模组不同核；**禁止**改 `Sdhci/Wifi*.c` 数据通路「顺便修 BT」 |
| 刀 0 | 只做探针：时钟/GPIO/UART 是否出字、固件文件是否可读；无通路则 **正式 skip**，不要假 Ready |

### 1.3 NUC 对表（只抄契约）

| 抄 | 不抄 |
|---|---|
| `HalBluetooth*` 返回值语义；Ready 条件（Reset+读址成功） | xHCI Bulk/Intr 端点代码整段搬到 UART |
| 本地名 `Blocks`；壳 `bt` 展示格式 | bt-defer / Worker Reveal（Duo console-only 无桌面） |
| HCI Reset / Read_BD_ADDR / Write_Local_Name 操作码与完成事件 | Inquiry/可见性（可后刀） |

---

## 2 · 刀序（严格）

> **推荐序：先 Pi 再 Duo**（Pi 资料与课轨更齐；Duo 先探针可与 Pi-1 并行文档，但代码勿两板同时大改）。  
> 协作暗号：执行者 **JX 只做文首 ★ 一刀**；未说 TG 不 commit。

### bt-pi-0 · 串口与固件钉死（只读/清单）

| 项 | 内容 |
|---|---|
| **做** | 写 `Docs/25-PI-蓝牙UART与固件清单.md`：① 壳用哪根 UART；② HCI 用哪根；③ `BT_REG_ON` GPIO；④ `.hcd` 如何进 `BLOCKS:`/`RES:`；⑤ 与现网 WiFi 并存注意 |
| **判据** | 清单有表可勾；**无代码行为变更**或仅加日志开关 |
| **禁止** | 直接改 `HalBluetooth.c` 声称 Ready |

### bt-pi-1 · UART 字节通路 + HCI Reset

| 项 | 内容 |
|---|---|
| **做** | Board 包：UART 开关电 → 下发 `.hcd`（对表 `hci_bcm` 流程）→ 发 **HCI_Reset** → 等 Command Complete |
| **判据** | 串口 `BT: HCI Reset ok`（或同形）；失败可诊断、不卡死壳 |
| **战场** | `HAL/Arm64/Board/rpi-3b/**` 新目录（建议 `Bluetooth/`）；`HAL/Arm64/Hal/HalBluetooth.c` 改接板实现 |
| **禁止** | 动 Duo；动 NUC XHCI BT；动 WiFi |

### bt-pi-2 · BD_ADDR + 名 + Ready

| 项 | 内容 |
|---|---|
| **做** | Read_BD_ADDR；Write_Local_Name(`Blocks`)；`HalBluetoothReady()==1` 当且仅当 Reset+读址成功；壳 `bt` 同 NUC 信息量 |
| **判据** | 真机串口见非零地址 + 名；`Ready=1`；冷启可重复 |
| **手测** | Pi 3B+；**CK** 刷盘或 ksend 热更（对表现网 Pi 部署） |

### bt-duo-0 · 有无 BT（探针）

| 项 | 内容 |
|---|---|
| **做** | 对照 SDK：HS-UART 基址、复位 GPIO、`fmacfwbt` 路径；串口打印「见/不见」证据 |
| **判据** | 二选一写死：`BT: duo path = UART@…` **或** `BT: skip (no HS-UART/fw)` |
| **禁止** | skip 时 Ready=1；改 Wifi dhcp/ping |

### bt-duo-1 ·（仅当 duo-0 有通路）Reset → 址 → Ready

| 项 | 内容 |
|---|---|
| **做** | 对齐 bt-pi-1/2 的语义，总线换 AIC 厂商流程 |
| **判据** | 同 bt-pi-2；或文档否证「固件/UART 不通」并保持 Ready=0 |

### 后排（本包不做）

- Inquiry / `bt scan` / 可发现（NUC btx-4/5 级）  
- BLE / GATT / A2DP / HID-over-BT  
- 抽公共「UART-HCI 大框架」跨 Pi/Duo（**两板都 Ready 后再考虑**）

---

## 3 · 铁律（评审红线）

1. **标准优先**：HCI 命令/事件以蓝牙 Core + 厂商 UART 绑运为准；禁止猜波特率狂扫。  
2. **有注释先看注释**：Pi WiFi / Duo WiFi 注释里的 keep/勿抢口，BT 不得对着干。  
3. **鼠优先**：若 Pi 桌面开着 HID，BT 固件泵/轮询不得长时间关中断或霸串口饿死输入（Duo console 则保证壳不假死）。  
4. **门面不撒谎**：无 Reset+BD_ADDR → `Ready=0`。  
5. **WiFi 隔离**：Duo `Sdhci/Wifi*`、Pi WiFi 目录默认只读；BT 新建树。  
6. **协作**：未口令不 commit/push；TTL 一根时注明接 Pi 还是 Duo。

---

## 4 · 交付物（每刀收口）

| 交付 | 说明 |
|---|---|
| 代码 | 板包 + Hal 接线；≤300；命名自查 |
| 串口原文 | 可 grep 的判据行（Reset ok / addr= / Ready） |
| 短清单 | 更新 Docs/25 或 Duo 对应 Docs 的「真机」表 |
| 否证 | 走不通就写清变量与现象，**停**，勿连开三刀 |

---

## 5 · 评审清单（Cursor 用）

- [ ] 是否改了 Wifi 数据通路 / dhcp flags？  
- [ ] Ready 是否在未读到 BD_ADDR 时仍为 1？  
- [ ] Pi 是否未论证就占用了调试串口？  
- [ ] 是否出现禁缩写 / 单文件 >300？  
- [ ] 行为是否与 NUC `HalBluetooth*` 语义同形（仅总线不同）？  
- [ ] 手测板与口令（CK/JX）是否在收口说明里写清？

---

## 6 · 建议开场口令（给人）

1. 活档 ★ 设为 `bt-pi-0`（或日档新页引用本文）。  
2. 对 DeepSeek：`JX` + 「只做 Docs/24 的 bt-pi-0」。  
3. 过则 `TG`；再 `JX` bt-pi-1。  
4. Duo 在 bt-pi-2 绿后开 bt-duo-0；若要抢时间，仅允许 **bt-duo-0 文档探针** 与 bt-pi-1 **并行**（两人/两会话），代码仍分树。
