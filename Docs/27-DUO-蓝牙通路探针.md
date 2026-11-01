# 27 · Duo S 蓝牙通路探针（bt-duo-0 眼睛交付）

> 交付对象：[`Docs/24`](24-板端蓝牙-Pi与Duo执行包.md) § bt-duo-0。  
> 性质：**只读调查结论**，不含行为代码；`HalBluetoothReady` 保持 0。  
> 引用一律给到文件/行或 SDK dts / pinmux，**无一处靠猜**。

---

## 0 · 一句话判据（Docs/24 二选一）

```text
BT: duo path = UART4@0x041C0000
```

**不是** `BT: skip (no HS-UART/fw)`。  
板级有独立 HCI UART（UART4）、模组电源脚、SDK 内 `fmacfwbt_*.bin` 与 `aic8800_btlpm`；Blocks 现网尚未接线，但**通路存在**。

> 术语澄清：Docs/24 / 1030 写的「HS-UART」在 CV181x/SG2000 上即 **DW-APB `uart4`**（与壳 UART0 同族 IP），不是另一套神秘控制器。

---

## 1 · 十问十答（对表 Docs/24 §6 Duo 列）

| # | 问题 | 结论 | 证据 |
|---|---|---|---|
| 1 | 壳用哪根串口？ | **UART0** @ `0x04140000`，DW-APB 16550，`reg-shift=2`，115200；USB-TTL 接板载调试口 | `HAL/RiscV/Board/milk-v-duo-s/BoardConfig.h:13-16`；`README.md:13` |
| 2 | HCI 用哪根 UART？基址？ | **UART4** @ **`0x041C0000`**（同族 DW-APB）。U-Boot 把 **UART2 焊盘**复用成 **UART4 功能**（含 CTS/RTS） | 基址：`build/boards/default/dts/cv181x/cv181x_base.dtsi:293-300` `uart4: serial@041C0000`；pinmux：`…/sg2000_milkv_duos_musl_riscv64_sd/u-boot/cvi_board_init.c:44-49` `// WIFI/BT` → `UART2_{RX,TX,CTS,RTS} → UART4_*`；板 dts `&uart4 { status="okay"; }`（同目录 `sg2000_milkv_duos_musl_riscv64_sd.dts:107-109`） |
| 3 | 电源 / 复位 GPIO？ | **模组上电与 WiFi 共用**：`poweron-gpio = GPIOA15`（ACTIVE_HIGH）。另有 `wakeup-gpio = PORTE7`（LPM / host wake，给 `aic8800_btlpm`） | dts `&wifi_pin`：`…/sg2000_milkv_duos_musl_riscv64_sd.dts:84-88`；Blocks 已用：`Sdhci/Private.h` `DUO_WIFI_POWER_BIT`、`Sdhci/Power.c` |
| 4 | 固件文件与加载序？ | SDK 有 **`fmacfwbt_8800d80_u02.bin`**（333 388 B）。AIC：**经 SDIO 灌 BT 共存 WiFi 固件**（`wl_fw=fmacfwbt_…`），再由 **UART H4** 跑 HCI。波特率/流控写进 patch 表：`btport=UART`、`baud=1500000`、`flowctrl=1` | 文件：`Source/DUO/sdk/…/rootfs_overlay/duos/mnt/system/firmware/aic8800/fmacfwbt_8800d80_u02.bin`；表：`aic_bsp_main.c`（`wl_fw = "fmacfwbt_8800d80_u02.bin"`）；默认：`aic_bsp_driver.h` `AICBT_BTPORT_UART` / `AICBT_UART_BAUD_1_5M` / `FLOWCTRL_ENABLE`；Blocks 现灌 **`fmacfw.bin`（无 BT 字）**：`Sdhci/WifiPrivate.h:170`、`WifiFw.c` — duo-1 须改路径或另拷 `fmacfwbt` 入 `Assets/Fw/aic8800/` |
| 5 | Reset 成功判据串口串？ | 拟与 Pi/NUC 同形：`Boot: Bluetooth UART4 up` → `HCI Reset ok` → `HCI BD_ADDR=…` → `Bluetooth ready`（duo-1 落地） | Docs/24 §3；本刀不发 HCI |
| 6 | Ready 条件？ | **同形**：Reset CC status=0 **且** `Read_BD_ADDR` 非全 0。本刀 Ready **保持 0** | `HalBluetooth.h`；现网 `HAL/RiscV/Hal/HalBluetooth.c` stub |
| 7 | Init 时机？ | console-only：**启动末一次**或壳 `bt init`；**不挡进壳** | Docs/24 §6 #7；对表 Pi |
| 8 | 与 WiFi 冲突面？ | 同模组：**SDIO=WiFi 数据**，**UART4=BT HCI**；电源脚共享 GPIOA15。**禁止**在 `WifiPatchTable.c` 里改 `btmode` 冒充 HCI 驱动；**禁止**为试 BT 关 SDIO/关 WiFi 时钟 | Docs/24 §2.2；`WifiPrivate.h:247-255` 的 `AIC_BT_*` = coex/patch 常量 |
| 9 | 拟新建文件名（≤300）？ | `HAL/RiscV/Board/milk-v-duo-s/Bluetooth/`：`UartTransport.c`（UART4+pinmux/RTSCTS）、`FirmwareLoad.c`（或复用 WiFi 灌 `fmacfwbt` 的文档步骤）、`HciCommand.c`、`BringUp.c`；**与 `Sdhci/` 分树** | Docs/24 §5.3 |
| 10 | 手测口令？ | 串口仍接 **UART0**；部署只拷 `Kernel.elf`→LABEL=`boot`（**禁止** `ck-pi-sd`）；duo-1 后冷启看四条 Boot 串 + 壳 `bt` | Docs/24 §2.2 部署行 |

---

## 2 · `aic8800_btlpm` 是什么（否证「它就是 HCI」）

| 项 | 事实 |
|---|---|
| 路径 | `linux_5.10/drivers/net/wireless/aicsemi/aic8800/aic8800_btlpm/aic8800_btlpm.c` |
| 职责 | **bluesleep LPM**（`host_wake` / `ext_wake` GPIO、与 `uart_port` 联动休眠） |
| 非职责 | **不**实现 H4 收发、**不**下发 `.bin` HCI 命令、**不**替代 `hci_uart` |
| 板配 | Duo S defconfig：`CONFIG_AIC8800_BTLPM_SUPPORT=m` + `CONFIG_BT_HCIUART=y` + `CONFIG_BT_HCIUART_H4=y`（`…/linux/cvitek_sg2000_milkv_duos_musl_riscv64_sd_defconfig:51-60,140`） |

⇒ duo-1 主参考应是：**UART4 字节通路 + H4 HCI**（对表本仓 Pi / NUC），btlpm 最多后排做唤醒。

---

## 3 · Blocks 现网缺口（duo-1 开工清单，本刀不改代码）

| 缺口 | 说明 |
|---|---|
| 无 UART4 驱动 | `HalSerial` / 壳只绑 `BOARD_UART_BASE=0x04140000`（UART0） |
| 无 `Bluetooth/**` 板树 | `HalBluetooth.c` 仍 stub，`Ready=0` |
| 未入库 `fmacfwbt` | Runtime/`Assets/Fw/aic8800/` 现用 `fmacfw.bin`；SDK 有 `fmacfwbt_8800d80_u02.bin` 可拷 |
| pinmux | Linux/U-Boot 已在 `cvi_board_init` 配好；裸机需确认冷启后 UART2 垫仍为 UART4 功能，否则 duo-1 先写 FMUX |

---

## 4 · 加载序草稿（duo-1，对表 AIC + Docs/24）

```text
① 保持 / 确认模组上电（GPIOA15；WiFi 已通则可复用）
② SDIO：灌 fmacfwbt（或确认现网 patch 的 btport=UART 对 HCI 足够——以真机 CC 为准，禁止猜）
③ 开 UART4 @0x041C0000：1.5 Mbps + RTS/CTS（AIC 默认）；H4
④ HCI_Reset(0x0C03) → CC
⑤ Read_BD_ADDR(0x1009) → 非零
⑥ Write_Local_Name("Blocks")
⑦ Ready=1 当且仅当 ④+⑤
```

**禁止**：扫波特率；改 `Sdhci/Wifi*` 数据面「顺便」；skip 却 `Ready=1`。

---

## 5 · 手测（本眼睛刀）

文档刀：**不要求**上电验 HCI。  
评审抽查：能按上表点开 dtsi / `cvi_board_init.c` / SDK `fmacfwbt` 路径即可。

---

## 6 · bt-duo-1 真机否证（2026-11-02）

| 项 | 结果 |
|---|---|
| 部署 | ksend `BOOT:boot.sd`（本卡冷启走 FIT；仅拷 `Kernel.elf` 不够） |
| 壳 | ConsoleOnly → 须 `VirtMin` 挂 `bt`；`bt init` → `HalBluetoothInitialize` |
| 串口原文 | `bt: diag=HCI Reset fail (fw/UART?)`；`Ready=0` |
| 解释 | 现网 SDIO 仍 `fmacfw.bin`（无 BT 共存字）；本刀**不灌** `fmacfwbt`、**不改** `Wifi*` |
| Docs/24 | 走判据第二条收官；Ready=1 另刀 |
