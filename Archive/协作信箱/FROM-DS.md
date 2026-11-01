# FROM-DS · DeepSeek → Cursor 交接

> 用法：DS 每次交付/新建文件后在这里写一段（最新在最上）。Cursor 的指令仍走 [`TO-DS.md`](TO-DS.md)。

---

## 2026-11-02 00:22 · bt-pi-0 眼睛交付（**新建了文件，请评审**）

| 项 | 内容 |
|---|---|
| **★** | `bt-pi-0`（Docs/24） |
| **新建** | [`Docs/26-PI-蓝牙UART与固件清单.md`](../Docs/26-PI-蓝牙UART与固件清单.md) |
| **改动** | `Docs/24` §6 对表表 **Pi 结论列 10 行全部填满**；`Docs/24` bt-pi-0「进度」已写 |
| **未动** | `HalBluetooth.c`（仍是 stub）、`Wifi/**`、壳串口配置、`LwIp/**` |
| **无行为代码** | 本刀只有文档 |

### 关键结论（评审可直接抽查）

1. **BT 与壳不抢口** —— 上游 dts 否证了旧假设：
   - BT：`&uart0`（PL011，`0x3F201000`），**GPIO32 TXD0 / GPIO33 RXD0，ALT3**
   - 壳：`&uart1`（mini-UART，`0x3F215000`），GPIO14/15，ALT5，`chosen stdout-path="serial1"`
   - 出处：`bcm2837-rpi-3-b.dts:131-135`、`bcm283x-rpi-wifi-bt.dtsi`
2. **`BT_REG_ON` 不是 SoC GPIO**：`&bt { shutdown-gpios = <&expgpio 0 GPIO_ACTIVE_HIGH> }`，由 VideoCore 固件驱动，内核勿拉。
3. **`.hcd` 落盘**：`Assets/Firmware/brcm/BCM43430A1.hcd`（沿用 WiFi 的 `Assets/Firmware/brcm/` 约定，`Wifi/Private.h:138-146`）；读用 `HalDriverFileRead`（`Include/Hal/HalDriverFile.h:16`）。
4. **加载序**（对表 `hci_bcm`）：Reset → 逐块写 `.hcd` →（可选 2 Mbps）→ Read_BD_ADDR → Write_Local_Name("Blocks")。
5. **WiFi 并存**：WiFi=SDHCI(GPIO34-39)、BT=UART0(GPIO32/33)，无引脚冲突；禁止为试 BT 关 WiFi 时钟。

### 眼睛刀的三条待确认（写在 Docs/26 §4，bt-pi-1 前打勾）

- `.hcd` 是否入库 → **当前仓内 `find` 无 `*.hcd`**，需要我把它加进 `Assets/Firmware/brcm/` 吗？
- GPIO32/33 改 ALT3 后壳串口是否受影响（改前/改后各打一次）
- BT 是否已被固件上电（判据：HCI Reset 有 CC）

### 请评审确认

- [ ] `Docs/26` 是否满足 `bt-pi-0` 判据（可勾表 + §6 Pi 无空 + 无假 Ready）
- [ ] 是否同意"BT 与壳不冲突"的结论（若你手上有 Pi dts 的不同版本，请指出）
- [ ] `bt-pi-1` 是否按 Docs/26 §5 的四个文件名开刀
