# Pi3B+ DWC2 HID 排障历程（rpi-7）

> **日期**：2026-10-24 夜 → 2026-10-25 凌晨（回家 SD 手测）  
> **硬件**：Pi 3B+ · LAN9514 板上 hub · Logitech 鼠 `vid=046d pid=c09d`（hub FS 口）· HDMI 录像优先 · CoolTerm 常丢字  
> **目标（rpi-7）**：桌面见软件光标；晃鼠跟手；键可后置  
> **代码**：`HAL/Arm64/Board/Raspberry/Pi3BPlus/Dwc2.c` · `HalDevicesInput.c` · `Core/Driver/Input.c` · `Gui/Cursor` · `HAL/Common/HalVideo.c`（FB dcache）  
> **主参考**：**DWC2/BCM 规格 + U-Boot dwc2**（+ 仓内 X64 Hub **形状**）；**勿**把 EDK2 当 Arm64/DWC2 主指南  
> **日档**：[路线图1024 · rpi-7](../../路线图1024.md#rpi-7) · 板备忘 [`NOTES.md`](../../../Kernel/HAL/Arm64/Board/Raspberry/Pi3BPlus/NOTES.md)  
> **姊妹**：[启动排障](Pi3BPlus真机启动排障历程.md) · [NUC 键鼠排障](真机复合USB键盘排障历程.md)（xHCI 对照，勿硬套寄存器）  
> **状态**：✅ **鼠** 真机过关（2026-10-25 ~02:23：流畅、图标可双击开）；🔧 **键** 尚未枚举/绑

---

## 0. 一句话（当前结论）

1. **Hub 枚举**：根口 LAN9514 `vid=0424 pid=9514`；`HCDMA` 必须 `phys|0xC0000000`（否则 DESC 全 0 → SET_ADDR STALL）。  
2. **FS HID 在 HS hub 后**：须 **SPLIT**；控制/批量 **`HCSPLT.XACTPOS=ALL`**（MID=0 → `HCINT` XACTERR `0x82`）。  
3. **ep0 首包 MPS=8**（USB2 FS 默认）；曾 `getdesc18` 用 64 → 屏上 `SPLIT getdesc18 fail`。  
4. **Attach**：`INPUT_BACKEND` 鼠路径要 `Poll`+`Mouse*`；`KeyboardDequeue` 可 NULL（已放宽）→ **键仍未绑**。  
5. **无光标（鼠 LED 已亮、枚举 OK）**：桌面 `PresentFlush` 会清 FB dcache；光标只走 `PresentCursor` **曾未清** → 箭头困在 CPU D-cache，VC/HDMI 看不见。修：`HalVideoPresentCursor` → `FbDcacheSync`；Reveal 走 PresentCursor。  
6. **底边两白块**：HOLD 行缺字形「豆腐」（`—` 等），**不是**光标；晃鼠桌面盖住就没了。  
7. **CoolTerm 残字**：mini-UART 无流控；**以 HDMI 录像 / `watch-boot-video` 为准**，勿凭残串口改波特率。  
8. **真机收口（鼠）**：PresentCursor dcache 后光标流畅，桌面图标可双击打开。

---

## 1. 证据从哪来

| 源 | 用途 | 坑 |
|----|------|-----|
| HDMI 录像尾帧 | 屏上 `Boot:` / `SPLIT` / `HID vid=` / 桌面有无箭头 | 日志滚动快 → HOLD；Agent 用 `Build/tools/watch-boot-video.sh` |
| CoolTerm | 有则加分 | 常丢半行；**禁止**当唯一证据改 USB |
| 鼠 LED | 设备上电/配置侧线索 | 亮 ≠ 光标已 Present；亮+不动优先查 Reveal/dcache |

**少换卡**：无新假设不刷；CK=`ck-pi-sd`（刷完弹出）；未说 TG **不** commit/push。

---

## 2. 方法编年（试错 → 过关）

### 2.1 Host / Hub（已过）

| # | 尝试 | 现象 | 结论 |
|---|------|------|------|
| H1 | Probe + 根口 reset | GSNPSID / HPRT 速度可读 | 基址 `0x3F980000` OK |
| H2 | HCDMA=CPU phys | hub DESC 全 0；SET_ADDR STALL | **必须**总线别名 `\|0xC0000000` |
| H3 | hub SET_CFG + 口 power | `vid=0424 pid=9514`；`hub+5`；外接口 FS | LAN9514 形状钉死 |
| H4 | 跳过 hub 上 HS（网） | 只扫 FS/LS 口绑 HID | 鼠优先路径 |

### 2.2 SPLIT + HID 枚举（已过）

| # | 尝试 | 现象 | 结论 |
|---|------|------|------|
| S1 | SPLIT 控制，ep0 先按 FS→64 GET 18 | 屏：`SPLIT getdesc18 fail`；鼠不亮 | **首包 MPS=8** → 再 18 |
| S2 | `XACTPOS` 默认 0（MID） | `HCINT=00000082`（CHHLTD\|XACTERR）；`getdesc8 fail` | 非周期 SPLIT → **XACTPOS=ALL**（Synopsys / Linux dwc2） |
| S3 | 复位后立刻当 HS | 偶发错路径 | 复位后跳过误判 HS；FS 走 SPLIT |
| S4 | XACTERR 后重 HcInit | 对标 U-Boot 重试形状 | 可保留；勿缩超时碰运气 |
| S5 | boot 协议 + 中断 IN | `HID vid=046d pid=c09d`；`mouse ep= ok` | 枚举过关 |
| S6 | `DriverInputAttach` 拒 NULL 键 | Attach 失败 | 放宽：键可后置 |

### 2.3 光标可见 / 跟手（🔧 待手测收口）

| # | 尝试 | 现象 | 结论 |
|---|------|------|------|
| C1 | 仅 Attach + 默认 Worker Reveal | 鼠亮；桌面有图标无箭头 | Pi `gTimerIrq=0` → CondResched/OnTimer 不抢占；Worker Reveal 易卡住 |
| C2 | 桌面后若 `HalMousePresent` 早 Reveal；`!HalTimerUsesIrq` 时 Shell 泵鼠 | 仍无箭头（录像） | 泵路径通了仍看不见 → **Present 侧** |
| C3 | 猜：未露前狂泵 / 饿死 | 与「有桌面、晃一下白块没」不符 | 鼠优先仍要守；本件主因不是 starve |
| C4 | `HalVideoPresentCursor` + `FbDcacheSync`；Reveal 调 PresentCursor | CK ~02:21 | **主修**：与整屏 Present 同清 FB；对齐 rpi-5 花屏教训 |
| C5 | 底两白块 = 光标？ | 晃鼠后消失 | **HOLD 豆腐**；勿当光标坐标 |

---

## 3. 坏相速查

| 看见 | 含义 | 下一步（勿猜） |
|------|------|----------------|
| hub DESC 全 0 / SET_ADDR STALL | HCDMA 未走 `0xC0000000` | 查 `DWC2_PHYS_TO_BUS` |
| `SPLIT getdesc18 fail` | FS ep0 首包 MPS 错 | 固定 8→18 |
| `HCINT=…82` / `getdesc8 fail` | XACTERR；常 XACTPOS≠ALL | 控制 SPLIT 置 ALL |
| `HID vid=` + `ep= ok` + Attach，无箭头 | Present/dcache 或 Reveal | 先确认 PresentCursor 清 FB；再查 Reveal |
| 底边 1～2 白方块，晃没了 | HOLD 缺字豆腐 | 忽略；看屏心有无箭头 |
| CoolTerm 残字 / 无字，屏有桌面 | mini-UART 丢字节或接线 | 录像；线：GPIO14→TTL RX |
| 鼠灯不亮 | 枚举未到 SET_CFG/协议 | 先修 SPLIT/getdesc，勿先改 Gui |

---

## 4. 仍开着

- [x] 手测：屏心软件箭头 + 晃动跟手 + 图标双击（2026-10-25）  
- [ ] **键盘 HID**：枚举 boot 键 + `KeyboardDequeue` + 与鼠同 hub 分口（鼠优先勿饿死）  
- [ ] 未露光标：Worker/HotPoll 勿狂泵鼠抢环（复查）  
- [ ] 定时器 IRQ / 协作调度加固（现靠早 Reveal + `HalTimerUsesIrq` 旁路）  
- [ ] 串口接线稳态（次要；不挡键鼠课感）

**禁止再踩**：无证据改波特；EDK2 当 DWC2 主参考；半截「关 Gui 坐标、底层仍泵 HID」。

---

## 5. 修订记录

| 日 | 记 |
|----|----|
| 2026-10-25 | 初稿：汇总 hub 别名、SPLIT MPS/XACTPOS、Attach、Reveal/timer、PresentCursor dcache、HOLD 豆腐；链到日档 rpi-7 |
| 2026-10-25 | **鼠 ✅**：真机流畅、图标可双击；键仍无 → 下一刀键盘 HID |
| 2026-10-25 | 录像 `02-22-32`：仅 `hub+4` → `046d:c09d` 鼠；`+2/+3/+5` 无 CONNECT；**无 kbd 枚举行** → 键未进主机或未插 Pi 口。串口 HOLD `GPFSEL1/CNTL` OK、`LSR=0`；CoolTerm 哑仍先查 GPIO14→TTL RX |
| 2026-10-25 | 录像 `02-30-10`：同上；见 `skip mouse+extraHID as kbd`（鼠附加 HID，**正确拒认**）+ `Attach mouse`、无 `kbd ep`；U-Boot 仍 `4 USB Device(s)`＝无第二 HID。键未进总线，非 Dequeue 坏。屏加 `no kbd (plug…)` |
| 2026-10-25 | **澄清**：能敲 U-Boot ≠ USB 键已进 Blocks。U-Boot 字可能走 CoolTerm RX；「无日志」＝ TX。用户确认 Pi 上另有 USB 键一直插着 → 录像仍无第二 CONNECT，疑 **CSFTRST 后键慢热** 被单轮扫漏。改：上电 +200ms、hub **3 轮**扫、已成功口不重 reset |
| 2026-10-25 | 录像 `02-46-06`：三轮扫后 **hub+3 LS** `st=0303`；`getdesc18` → `HCINT=0x402`（CHHLTD\|DATATGLERR）`spl` 带 COMPSPLT；鼠仍 ok、`no kbd after hub passes`。修：CSPLIT 等下一微帧（USB2 §11.18）+ DATATGLERR 重试 + HCFG FSLSPClkSel |
| 2026-10-25 | 录像 `02-53-02`：仍 `getdesc18 fail mps=8` @ hub+3 LS（微帧等待不够）。改：USB2 序 **GET8→SET_ADDR→GET18@addr**（addr0 满 18 失败不弃口）。串口：mini-UART 行末等 **TX idle(LSR.6)**（无流控暴打易丢；屏仍为准） |
| 2026-10-25 | CoolTerm 残串口仍有用：`hub+3 LS` + **`HID vid=2a7a pid=939f`**（CASUE USB KB，Linux 03/01/01）+ `getdesc18@addr fail` + 仅 `Attach mouse`。说明键已露脸；满 18 仍脆。次日刀：LS 在 GET18 失败后仍 **SET_ADDR→GET_CFG**（MPS 已从 GET8 知，勿因 desc18 弃口） |
| 2026-10-25 | **落地**：LS `getdesc18@addr` 失败 → `skip desc18 → GET_CFG`，不再 `return -1` |
| 2026-10-25 | 串口/录像：跳过 desc18 后 **`HID cfg fail`**（hdr 已过）。改：LS 先 cfg **GET8**；满长失败则短包/32·48·64 重试，Parse 够用即绑 |
| 2026-10-25 | 录像 `03-12-19`：`skip desc18` → **`cfg short=000b/003b`** → `no boot hid`。11 字节不够 iface+ep。改：CLEAR_TT_BUFFER 后重试至 ≥25 或满长 |
| 2026-10-25 | 录像 `03-15-47`：ClearTT 后变 **`cfg fail`**（更差）。改：LS 控制包间跨 2 微帧；cfg **按 8 递增重取**；仍不够则 **boot-fallback ep1**（CASUE 类） |
| 2026-10-25 | **真机键 ✅**（boot-fallback 路径）：桌面/Shell 可打字；鼠回归仍可用 |
