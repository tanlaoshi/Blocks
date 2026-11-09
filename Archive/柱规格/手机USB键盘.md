# 柱规格 · 手机 USB 键盘（Pixel 8 / MIX 2）

> **状态**：政策已放行；**`PR-PIXEL8-USB-3/3a` ✅**（`k a2` + `KEY>`；Caps/小键盘）。PIXEL8-2 splash ✅；USB-1/2 ✅。MIX2：splash/USB-2 ✅；USB-3 ⏸。活 ★（手机）空 → [`路线图1108.md`](../路线图1108.md)。  
> **活排期**：[`路线图1108.md`](../路线图1108.md)。无输入法：HID Boot 键码直通壳即可。  
> **Pixel 试错史**：[`过程/排障/Pixel8-USB-hub枚举试错历程.md`](../过程/排障/Pixel8-USB-hub枚举试错历程.md)。  
> **MIX2 试错史**：[`过程/排障/MIX2-USB键盘试错历程.md`](../过程/排障/MIX2-USB键盘试错历程.md)（半程归档；有串口证据后再回灌）。  
> **史**：bringup [`进展/路线图1107.md`](../项目/进展/路线图1107.md)；MIX2-2 ✅；USB-1/2 ✅。  
> **产品名**：Blocks。口令同全表。

---

## 1. 目标（一句话）

两台已放行手机上，**USB-C 接 hub → 键盘可敲进 Blocks 壳**（与现网 Pi/NUC HID 同形语义）。

| 机 | 已有输出 | 本柱输入 | 验收 |
|---|---|---|---|
| **MIX 2** | splash 彩条 + `Blocks ready` | USB hub 键盘 | 键入可见于 splash 行缓冲或等价回显 |
| **Pixel 8** | ★ splash（同 MIX2）；Cereal 仅备份 | 同上 | 键入可见于 splash；不依赖 Cereal |

**不在本柱**：重配/初始化 DSI 面板驱动、触摸、UFS、网、完整桌面、刷入 boot 分区（仍只 `fastboot boot`）。**可写 ABL 已扫 splash**。

---

## 2. 目标是否合理？

**合理，且适合当「bringup 后第一输入柱」。**

| 赞成 | 注意 |
|---|---|
| 输出已齐（MIX splash / Pixel 串口），缺的是输入闭环 | 手机 USB-C **Host/OTG** 与电脑 xHCI 不同：角色切换、VBUS、Type-C CC |
| hub+键盘比「先上触摸/UFS」边界清晰，可对表 Pi DWC2 / NUC xHCI HID | 无厂商公开手册时，只准 **DT + DWC3/xHCI 公开编程模型**，禁止猜寄存器 |
| 与课堂「键鼠最优先」同向 | 外供电 hub 优先；手机口供电不够则标配「hub 需外电」 |
| 两台可共用 HID/壳路径，板差只在 Host 控制器 | Pixel（Tensor）与 MIX（MSM8998）**各开 Host 探测刀**，勿抄同一 MMIO |

**不合理的扩法**（本柱不做）：先做 GUI 桌面、先做 UFS root、先做无线。

---

## 3. 政策修订（相对旧红线）

| 旧 | 新 |
|---|---|
| 两台：屏 / 触摸 / UFS / **USB 主机** 一律不开 | **USB 主机仅为本柱**：Host 模式 + hub + HID 键盘；仍须公开规格/DT |
| 控制台 bringup 即收官 | bringup ✅ 后可排本柱；**其它手机仍不做** |
| 触摸 / 自研面板 / UFS | **仍不做**（未点名不开） |
| 只用 `fastboot boot` | **维持**（不 `flash` Blocks） |

权威表：[`项目/路线图.md`](../项目/路线图.md) §D（与本文同步）。

---

## 4. 架构拆分（板差 vs 共用）

```
┌──────────────┐    ┌──────────────┐
│ Pixel 8 板包 │    │  mix-2 板包  │
│ DWC3/xHCI?   │    │  DWC3 Host   │
│ 角色/时钟/PHY│    │  时钟/PHY    │
└──────┬───────┘    └──────┬───────┘
       │  BoardUsbHost*     │
       └─────────┬──────────┘
                 ▼
        共用：Hub 枚举 · HID Boot 键盘
                 ▼
        HalInput / FeedHid / Shell（对表现网）
```

- **板包**：控制器基址、IRQ/轮询、Host 模式、根端口供电。  
- **Common/现网**：hub、HID 报告、壳回显——**尽量复用**，禁止另起缩水 API。  
- **输出**：两台键回显都画在 splash；Pixel Cereal UART 仅备份排障。

---

## 5. PR 拆分（建议序）

> 序内从上到下；一刀一验收。Common 大改须明示。估刀为课堂量级，供排期不当事先承诺。

### 共性准备

| PR | 内容 | 验收 | 估 |
|---|---|---|---|
| **PR-PHONE-USB-0** | 规格与 DT 证据包：两台 USB Host/DWC3 节点、兼容串、供电约束；更新本档「证据」节 | 文档齐；无代码也可先过 | 0.5 |
| **PR-PHONE-USB-1** | 壳/HID 挂钩设计：无键时不饿死；有键走现网 `FeedHid` 形状 | 设计注记进板 README；可空 stub | 0.5 |

### MIX 2（splash 已绿，建议先打通输入）

| PR | 内容 | 验收 | 估 |
|---|---|---|---|
| **PR-MIX2-USB-1** | MSM8998 USB **Host 模式**最小：控制器可见、根端口上电/复位有日志 | `fastboot boot` 后标记 `usb: host`（此时可仍无坞） | 2–4 |
| **PR-MIX2-USB-2** | 枚举 **外置 hub**；**热插拔**（boot 后再插坞须能看见） | 按 §7.0 换线后见 hub addr | 2–3 |
| **PR-MIX2-USB-3** | hub 下 **HID Boot 键盘** → 壳（**后增**；半程 ⏸） | splash 见字符；现回 USB-2 树 | ⏸ |

### Pixel 8（splash ✅ · hub **活 ★**）

| PR | 内容 | 验收 | 估 |
|---|---|---|---|
| **PR-PIXEL8-USB-1** | Tensor USB **Host 模式**最小（对表 DT） | 串口 `usb: host` | ✅ |
| **PR-PIXEL8-2** | ABL splash + DECON TRIG；彩条 + `Blocks ready` | ✅ 左上彩条+字 | ✅ |
| **PR-PIXEL8-USB-2** | hub 枚举；状态画 splash | 坞直插；屏见 `e…a1` | ★ |
| **PR-PIXEL8-USB-3** | HID 键盘 → splash 回显 | 敲键出字 | 2–3 |

### 收口

| PR | 内容 | 验收 | 估 |
|---|---|---|---|
| **PR-PHONE-USB-9** | 两台手测清单 + README；红线重申 | 1108/本档勾绿；不 flash | 0.5 |

**合计粗估**：约 **14–22 刀课堂量**（Host 探测风险最大；Pixel 通常难于 MIX）。

---

## 6. 硬约束

1. **标准优先**：xHCI / USB2 hub / HID Boot；高通/Tensor 仅 Board 探基址与时钟。  
2. **有注释先看注释**：现网鼠优先、勿饿死 HID 等注释禁止对着干。  
3. **命名写全**；单文件 ≤300 行。  
4. **加载**：只 `fastboot boot`；Windows 共享可继续用 `\\ASUS\Users\Tank\mix2\`。  
5. **供电**：MIX 现网坞用 **Type-C 充电口接墙充**；勿依赖手机 lone 口供电。  
6. **未点名外设**（触摸/UFS/面板 init；本坞 HDMI/RJ45/读卡器）仍禁止借车。  
7. **键盘只认 USB-A**：不把「插在坞 Type-C 口的键盘」当验收路径（除非后补 PR 写明）。  
8. **电源键**：短按 → fastboot 以便再 `fastboot boot` — 见 [`路线图1109.md`](../路线图1109.md) `PR-PHONE-PWR-0`（先 MIX2）。

---

## 7. 手测硬件（已备）

### 7.1 MIX 2 用 Type-C 扩展坞（用户现网）

接入：扩展坞 **Type-C 公头 → MIX 2**。口布局：

| 口 | 数量 | 本柱用法 |
|---|---|---|
| Type-C 数据 | 2 | 本柱**不依赖**（勿插键盘当唯一路径） |
| Type-C **充电** | 1 | **接充电器**：给手机+坞供电（等同外电 hub） |
| **USB-A** | 2 | **键盘插这里**（HID Boot） |
| HDMI | 1 | **本柱不做**（Alt-Mode/显示另柱） |
| RJ45 | 1 | **本柱不做** |
| 读卡器 | 1 | **本柱不做**（存储/UFS 红线外也不借 MSC） |

手测默认：

1. 充电口接墙充（保证 VBUS/PD，避免口供电不足枚举不稳）。  
2. USB-A 插普通键盘。  
3. 其余口空着，减少枚举噪声（复合 hub 上常挂多功能器件）。

说明：此类坞内部通常是 **USB hub + PD +（可能）DP Alt-Mode/网卡/读卡**。本柱只验收 **Host→hub→USB-A HID 键盘**；HDMI/网/卡在日志里出现可忽略，禁止为它们开驱动。

### 7.0 单口矛盾：先 `fastboot boot`，再插坞（必读）

MIX 2 **只有一个 Type-C**。电脑占用该口做 `fastboot boot` 时，坞/键盘**不可能**同时接在手机上——这是正常的，**不要求开机瞬间键盘已在位**。

**推荐手测节拍：**

1. USB 线：手机 ↔ Windows，进 fastboot。  
2. `fastboot boot \\ASUS\Users\Tank\mix2\boot-chiron.img`（或其后带 USB 的板包）。  
3. 见到 splash / `Blocks ready`（或 `usb: host` 日志）后：**拔掉电脑线**。  
4. 插上 Type-C 坞；**充电口接墙充**；USB-A 插键盘。  
5. 等枚举（秒级；实现须 **热插拔/周期性扫根端口与 hub**，禁止「仅启动时扫一次」）。  
6. 敲键 → splash/壳见字符。

因此 PR 验收写的是：**Host 起来之后再接坞仍能出键**，不是「冷启时键盘已挂上」。

Pixel 若 Cereal 占调试、Type-C 另口 Host，可减少换线；仍建议支持热插拔，避免绑死启动顺序。

**坞上多余的 Type-C 数据口：**

| 阶段 | 能否用来连电脑 fastboot | 能否插键盘 |
|---|---|---|
| 手机仍在 fastboot / 未跑 Host | **不能**（下游口按 Host 外设设计；救不了单口换线） | 无意义 |
| Blocks USB Host 已起来 | 勿插电脑当刷机口 | **可以**（与 USB-A 同属下游 hub；验收默认仍 USB-A，C 口键盘等价后补） |

充电口：始终用于墙充。HDMI/RJ45/读卡：本柱不用。

### 7.2 证据（PR-PHONE-USB-0 填写）

| 项 | MIX 2 | Pixel 8 |
|---|---|---|
| SoC USB IP | **MSM8998 `snps,dwc3` @`0xA800000`**（线刷 DT `dwc3@a800000` / 父 `ssusb@a800000` `qcom,dwc-usb3-msm`） | **Tensor G3 `snps,dwc3` @`0x11210000`**（`device/google/zuma` `sys.usb.controller=11210000.dwc3`） |
| PHY | `qusb@c012000` `qcom,qusb2phy-v2`（后刀） | Samsung/Exynos 风格 glue（后刀）；本刀不碰 |
| 公开材料 | 线刷 DT + Synopsys DWC3 GCTL/GSNPSID；Linux dwc3 `PrtCapDir=Host` | zuma init.rc 控制器名 + Synopsys GSNPSID/GCTL；Cereal=SBU UART，与 DWC3 数据面分离 |
| Host 线材 | Type-C 坞 + **充电口接电**；§7.0 先 boot 再插坞 | Cereal 占 SBU 调试；Host 走 Type-C 数据脚（另备坞；USB-1 勿改 mux） |
| 键盘 | HID Boot；插坞 **USB-A**；无 IME | 同左 |
| 现网坞 | 绿联 **15375**：QUSB+QSCRATCH 后仍 `p*:200`；ssusb `extcon=pdphy` → 须 **SPMI Type-C/DFP**（非再拧 xHCI） | — |

---

## 8. 粘性码快查（续测）

完整表与编年见试错史 §1。手测最小集：

| 看 | 绿 | 红时报 |
|---|---|---|
| `e` | `a1` | 整段 `e…` |
| `k` | `a2` + 敲键出字 | `0` / `10–25` / `40+` / `3xx` |

---

## 9. 修订

| 日期 | 说明 |
| ---- | ---- |
| 2026-11-09 | USB-2：TCPC `tc1 r5 sw9` 已通；插 Hub 仍 `p*:200` → 下一环 eUSB repeater `11-003e`（同 `10cb`） |
| 2026-11-09 | USB-2 坞测：`p*:200 e0 a0` → 须 max77759 TCPC DFP（HSI2C `10cb0000`）；非再拧 xHCI |
| 2026-11-09 | **PIXEL8-2 ✅**：左上彩条+字；TRIG_CON@`0x19470030`；★→USB-2 |
| 2026-11-09 | **重拆**：Cereal 透传无 CCS → ★=`PR-PIXEL8-2` splash；USB-2 ⏸ 待屏+坞 |
| 2026-11-09 | **PIXEL8-USB-1 ✅**：`gctl` Device→Host `0x00011004`；`id=0x33313130`（非 5533，GCTL 活）；Cereal 通；曾→USB-2 |
| 2026-11-09 | PIXEL8-USB-1：钉 `0x11210000`（`11210000.dwc3`）；MMU 先过；保 Cereal |
| 2026-11-09 | MIX2 树回 USB-2（`e a1`）；USB-3 半程归档；★→PIXEL8-USB-1（后增键需求让路） |
| 2026-11-08 | 挂试错史；§8 手测快查；USB-3 在测（`e a1` 后盯 `k`） |
| 2026-11-08 | USB-2 开干：xHCI 起环 + `wait dock` 热插扫；待手测 `hub` |
| 2026-11-08 | **USB-1 ✅**：真机 splash 见 `id`/`host`；★→USB-2 hub 热插 |
| 2026-11-08 | 推星 MIX 优先；USB-0：DT 钉 `dwc3@a800000`；开干 USB-1 Host 探测 |
| 2026-11-08 | 登记 MIX 现网 Type-C 坞（双 C + 充电 C + 双 A + HDMI + RJ45 + 读卡）；验收只钉 A 口键盘+充电供电 |
| 2026-11-08 | 立项：改政策放行两台 USB Host→hub→键盘；拆 PR；目标合理性写入 §2 |
