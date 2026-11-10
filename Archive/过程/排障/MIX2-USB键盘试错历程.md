# MIX 2 · USB Host→hub→键盘 试错历程（归档）

> **日期**：2026-11-08～09  
> **结论（2026-11-10）**：**`PR-MIX2-USB-3/3c` ✅**（`k a2`+`KEY>`；锁灯仅锁键翻转；绿联坞二级先扫）。  
> 半程坑（中断无 TE / settle 狂扫 / Configure 早发 SET_REPORT→2711）仍有参考价值。  
> **柱史**：[`项目/进展/路线图1108.md`](../../项目/进展/路线图1108.md)。活 ★：[`路线图1110.md`](../../路线图1110.md)。  
> **柱规格**：[`手机USB键盘.md`](../../柱规格/手机USB键盘.md)。

---

## 0. 收口口径

| 项 | 状态 |
|---|---|
| MIX2 最初目标（splash `Blocks ready`） | ✅ `PR-MIX2-2` |
| USB-0 / USB-1 Host | ✅ |
| USB-2 hub 热插 `e a1` | ✅；**代码停此**（commit `1a28dc03` 线） |
| USB-3 HID 键 | ⏸ 曾配到 `D … e81 s3 m10`（EP Running），**中断无 Transfer Event（`l0`）**；后拧评分/重试一度 `k42`/`k22` |

---

## 1. USB-3 半程要点（回灌用）

- 硬件：绿联坞 **15375**；**充电口须墙充**；§7.0 先 `fastboot boot` 再插坞。  
- 真机曾稳定：`e a1` + 键槽 ConfigEP + `e81 s3 m10` + Proto=0（`r0 t2`）。  
- 根因未收：中断 IN 武装后无事件（非仅「没按键」）。  
- 失败教训：接口评分过严 → `k42`；枚举失败漏 `DisableSlot` + 狂重试 → 槽耗尽/`k22`。  
- Pixel 有串口后：先把「有无 Transfer Event / 完成码」钉死，再回 MIX2 对表。

### sticky 码（摘）

| 码 | 含义 |
|---|---|
| `e a1` | hub Address OK |
| `k 22` | 子口无 CONNECT |
| `k 42` | 解析中断 EP 失败 |
| `D pd3 … l0 e81 s3 m10` | 键已配、EP Running、无中断事件 |

---

## 2. 因果链（仍有效）

```
splash / DWC3 Host / Type-C OTG / hub e a1   ✅
    ↓
HID ConfigEP / 中断 IN 出字                 ⏸ USB-3
```

**板（供电/角色/PHY）先于 xHCI 玄学**；无 CC/DP 时不拧 hub。

---

## 3. 代码地图（USB-2 停驻时）

| 路径 | 职责 |
|---|---|
| `UsbHost/BringUp.c` · `XhciStart.c` | Host 起环 |
| `QusbPhy` · `Qscratch` · Type-C/OTG | 角色与供电 |
| `HubEnumerate` · `HubWatch` | 根口 hub（`e a1`） |

USB-3 半程源文件（Hid* / HubChild* / XhciInterrupt 等）**已从树删除**，细节只留本文。
