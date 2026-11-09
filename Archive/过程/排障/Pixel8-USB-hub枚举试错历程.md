# Pixel 8 · USB Host→hub 枚举试错历程（PR-PIXEL8-USB-2）

> **日期**：2026-11-09  
> **结论**：屏准 **`p1:e03 ea1 a1 u2 s0`** / **`c8 ua0`** / **`ls0`** / **`vba`** —— hub Address + GetDescriptor 绿。  
> **收口 commit**：`0ab9ec52`（仓交推 `origin/main`）。  
> **活 ★**：[`路线图1108.md`](../../路线图1108.md) → **`PR-PIXEL8-USB-3`**（HID 键）。  
> **柱规格**：[`手机USB键盘.md`](../../柱规格/手机USB键盘.md)。  
> **姊妹档**：[`MIX2-USB键盘试错历程.md`](MIX2-USB键盘试错历程.md)（USB-2 停驻 / USB-3 半程）。

---

## 0. 收口口径

| 项 | 状态 |
|---|---|
| PIXEL8-2 splash（彩条 + 字；DECON TRIG） | ✅ |
| USB-1 Host 探测 / Type-C DFP + VBUS | ✅ |
| USB-2 hub 热插 `ea1 a1` | ✅（本文）；**代码停此并已仓交推** |
| USB-3 HID 键 | ★ 排队 |

手测：**拔电脑线 → 插外供电 Type-C 坞**；**以 splash 为准**（Cereal/SBU 不稳，且占口时无 CCS）。

---

## 1. 因果链（已走通）

```
ABL splash / TCPC Host+VBUS / eUSB repeater Host(c8)
    ↓
DWC3 Host + 清 FREECLK + 清 SOFITPSYNC + GUCTL(REFCLKPER/NOEXTRDL/…)
    ↓
根口 PR → PED+HS(p1:e03) → EnableSlot → Address BSR1→BSR0 → GetDescriptor
    ↓
ea1 a1 ✅
```

**板（角色/VBUS/eUSB/DWC3 时序）先于 xHCI 玄学**；无 CCS/`c10` Device 时不拧 Address。

---

## 2. 证伪与拐点（按时间）

### 2.1 Cereal 透传永远无 CCS

- 现象：`p*:200`（仅 PP），插「透传」口无 CONNECT。  
- 根因：Cereal/SBU 是 **Device 上行备份串口**，不是 Host 下行数据口。  
- 拐点：改 **MIX2 同路**——splash 画 `p*/e*/c/vb/u/s`；坞 **直插** Type-C。

### 2.2 CONNECT / PED 绿，卡 `e304`

| 观察 | 含义 |
|---|---|
| `p1:e03` | CCS+PED+HS |
| `c8`（非 `c10`） | repeater PORT1 = Host |
| `vba` | buck-boost 源模开 |
| BSR1 ✅ / BSR0=`e304` | 上下文/DMA 可；**线上 SET_ADDRESS USBTE（cc=4）** |
| 曾 `601`→`e03` | PED 边沿 Speed 未锁 —— 已等 Speed≠0 + T_RSTRCY 真 ms |

### 2.3 已证伪（仍 `e304` 时）

| 尝试 | 结果 |
|---|---|
| 只加长 recovery / Speed settle | PED 更稳，仍 `e304` |
| **置** `GCTL.SOFITPSYNC`（误读 freeclk quirk） | 仍 `e304`；**与 Android 相反** |
| LOPWR@0x50：纠 bit1（0=寄存器关 UART）+清 HOST_FRAME_LP/IDLE_LP | 屏 **`ua0`**，仍 `e304` |
| Address 前等 dig SE0；失败码编入 ls | 屏 **`e304`+`ls0`** → **线态/J 不是主因** |

### 2.4 收口一刀（对表 zuma）

证据：`zuma-usb.dtsi` `adj-sof-accuracy=<1>` + `dwc3_exynos_core_init`：

1. 清 `GUSB2PHYCFG.U2_FREECLK_EXISTS`（保留）。  
2. **`adj-sof-accuracy` 覆盖 freeclk quirk → 清 `GCTL.SOFITPSYNC`**（勿再置）。  
3. `GFLADJ` fladj=`0x20` + 19.2MHz 域（保留）。  
4. **`GUCTL`**：`REFCLKPER=0x34`（19.2MHz ns）、HSTINAUTO、SPRS、**NOEXTRDL**。

刷后屏：**`ea1 a1`**。

---

## 3. sticky 码（splash · 本柱）

行宽紧；**以屏为准**，Cereal 易花/半截。

| 字段 | 绿 | 红/含意 |
|---|---|---|
| `p1:` | `e03`（CCS+PED+HS） | `200` 无连；`601` 有连无 PED |
| `e…` | **`a1`**（`0xA0\|addr`） | `e304` Address USBTE；`e31x` 上下文；`e19x` HS 后 dig 非 SE0 |
| `a` | `a1` | `a0` 未编址 |
| `u` / `s` | `u2 s0`（USB2 口位图 / SlotType） | — |
| `c` | **`c8` Host** | `c10` Device |
| `ua` | **`ua0`**（LOPWR 关 UART+帧 LP） | 旧误 `ua2`（bit1=GPIO 选源） |
| `vb` | `vba` | — |
| `ls` / `pd` | `ls0` SE0 + `pd3` 双下拉 | 曾见 `ls1`＝J（Address 前已等 SE0） |

`cm` 行偶发「半行」：多半行距/重画盖住，文案本身短，**不挡** USB-2 验收。

---

## 4. 代码地图（USB-2 停驻）

| 路径 | 职责 |
|---|---|
| `UsbHost/TypeCForceHost.c` | TCPC DFP + VBUS buck-boost |
| `UsbHost/EmbeddedUsbRepeater.c` | TUSB2E221@0x3e；LOPWR；`c`/`ua` |
| `UsbHost/EmbeddedUsbPhysical*.c` · `DualRole*` · `AliveClockPowerManager*` | eUSB CON/dig、BLKCON、ACPM LDO |
| `UsbHost/XhciStart.c` | Host；**清 SOFITPSYNC**；GUCTL；GFLADJ |
| `UsbHost/HubRootPort.c` | PR / PED / leavePRC；复位路径保持 SOFITPSYNC 清 |
| `UsbHost/HubEnumerate.c` | BSR1→BSR0 分诊；SE0 门；`e304`/`ea1` |
| `UsbHost/HubWatch.c` · `HubStatusPaint.c` | 热插单次 enum + splash |

板包：`Kernel/HAL/Arm64/Board/pixel-8/`（BRINGUP=1 → `boot.img` / `fastboot boot`）。

---

## 5. 教训（给 USB-3 / 回灌）

1. **标准 / DT / 姊妹实现优先**；「缺 SOFITPSYNC→USBTE」是错假设，以 `dwc3_exynos` + 屏为准。  
2. **BSR1 vs BSR0** 分开：`e31x`≠`e30x`，勿在 DMA 已绿时继续拧上下文。  
3. **Cereal ≠ Host 数据口**；排障默认 splash + 直插坞。  
4. TI LOPWR **bit1=1 是 GPIO 选源**（名字易反）；帧 LP 可关，但本机不是 `e304` 主因。  
5. 下一刀 HID：对表 MIX2 半程（有 Transfer Event 再谈评分）；Pixel 已有串口备份但屏仍优先。

---

## 6. 相关 commit（摘）

| commit | 说明 |
|---|---|
| `8924ee30` | USB-2 中途：eUSB CON + splash 状态 |
| `0ab9ec52` | **收口**：hub `ea1 a1`；清 SOFITPSYNC + GUCTL；LOPWR `ua0` |
