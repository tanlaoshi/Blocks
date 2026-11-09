# 柱规格 · X64 蓝牙鼠（罗技 MX Master 初代 · BLE）

> **状态**：**规格钉死**（`PR-BTM-0` ✅）；活 ★ 见 [`路线图1109.md`](../路线图1109.md)。  
> **活排期**：[`路线图1109.md`](../路线图1109.md)（★=`PR-BTM-3`；`PR-BTM-1…2` ✅）。  
> **前置**：NUC 课轨 `btx-1…5` ✅（USB HCI / Reset / BD_ADDR / 经典 Inquiry / visible）。  
> **靶机**：X64 NUC 板载 HCI（NOTES：`8087:0A2B`）；靶鼠：**罗技 MX Master 初代**（无 2S/3/3S 后缀）。  
> **产品名**：Blocks。口令同全表。  
> **接收器**：已丢失 → **只走蓝牙直连**（本柱不接 Unifying/Bolt）。

---

## 1. 目标（一句话）

NUC 上 **蓝牙直连初代 MX Master**，光标可拖、主键可点（与现网 USB/PS2 鼠同形 `HAL_MOUSE_REPORT`）。

| 项 | 本柱 | 非本柱 |
|---|---|---|
| 链路 | **BLE + HOGP**（HID over GATT） | 经典 BR/EDR HIDP；Unifying / Bolt 接收器 |
| 输入 | 相对位移 + 左/右/中键 → Gui 光标 | 手势键全映射、DPI、多机切换、Options+ |
| 课机 | NUC 板载 USB HCI | Pi/Duo 蓝牙鼠；外置 BLE 棒矩阵 |
| 配对 | Just Works；**本会话有效**（可不落盘） | 数字比对 UI、永久密钥库产品化 |

**验收一句话**：鼠底切到蓝牙通道并处于可被发现 → Blocks 扫到 → 连接/配对 → 拖动能跟手、左键可点。

---

## 2. 目标是否合理？

**合理，但是「半个 Wi‑Fi MVP」量级，不是轻量经典鼠柱。**

| 赞成 | 注意 |
|---|---|
| 接收器丢失 → BLE 是唯一路径；靶鼠明确 | 初代 Master 直连 = **BLE**，现有 `bt scan`（经典 Inquiry）**扫不到** |
| HCI 探针 / Reset / 名 `Blocks` 已绿；ACL 环已架 | ACL **未传业务**；无 LE Meta 泵、无 SMP、无 ATT |
| 光标注入可复用 `INPUT_BACKEND`（对表 USB/PS2） | 与有线 HID **共 xHCI** → 全程鼠优先，禁止饿死 |
| 对表业界：Linux `hid-logitech-hidpp` 将 Master 标为 **BT LE** | Report 模式 + HID++ 厂商页；MVP 只解「能动」的报告，勿先上全 HID++ |

**不合理的扩法**：先做 A2DP、先做键盘 BLE、先做 Pi/Duo 同柱、先做 Bolt 协议兼容。

### 与「经典蓝牙鼠」估时对照

| 路线 | 刀粗估 | 是否覆盖本鼠 |
|---|---|---|
| 经典 Inquiry → L2CAP/HIDP | 8–12 | ❌ |
| **本柱 BLE → SMP → HOGP** | **12–18** | ✅ |
| 另买 BR/EDR 老鼠 | 沿经典估时 | 与本靶无关 |

---

## 3. 现状底座（勿重做）

| 已有 | 证据 | 本柱用法 |
|---|---|---|
| USB HCI 枚举 / 端点 | `XHCI/Bluetooth/Probe.c` `Configure.c` | 事件 Interrupt + ACL Bulk（LE ACL 同走 Bulk） |
| HCI Reset / BD_ADDR / 名 | `Hci.c` `Info.c` | Ready 门槛不变 |
| 经典 `bt scan` / `bt visible` | `Scan.c` `Visible.c` | **保留**；鼠柱另加 `bt lescan` / `bt mouse` |
| 鼠优先分片 | `ServiceHidCompletions`；`bt-defer` | LE 扫/连/通知泵必须同纪律 |
| 光标链 | `MousePush` → `INPUT_BACKEND` → `GuiPollMouse` | **新建** `BluetoothHid` 后端，不占 USB 鼠槽 |

HAL 现门面（`HalBluetooth.h`）：无 Connect / LE / HID。本柱按刀扩展，禁止空 stub 谎称已配对。

---

## 4. 架构拆分

```
 USB HCI (已有 btx)
        │
        ▼
 ┌──────────────────┐
 │ HCI Host 泵       │  事件 demux + ACL 收发 + 完成包流控
 │ （本柱地基）       │  持续 ServiceHidCompletions
 └────────┬─────────┘
          │
          ▼
 ┌──────────────────┐
 │ LE：扫 / 连 / SMP │  LE Meta Event；Just Works
 └────────┬─────────┘
          │
          ▼
 ┌──────────────────┐
 │ ATT/GATT 客户端   │  发现 HID Service → Report Map / Report
 └────────┬─────────┘
          │ Notify
          ▼
 ┌──────────────────┐
 │ HOGP → 报告解析   │  位移+按键 → HAL_MOUSE_REPORT
 └────────┬─────────┘
          │
          ▼
   INPUT_BACKEND（第二后端）→ GuiPollMouse
```

- **Board/总线**：仅 NUC USB HCI；不改 Pi UART / Duo SDIO。  
- **禁止**：把 BLE 鼠塞进 `UsbHid/Mouse.c` 中断环；禁止与经典 Inquiry 共用「单次等一包」阻塞泵当常驻主机。

### 落点目录（钉死）

| 层 | 路径 | 说明 |
|---|---|---|
| USB HCI 运输 / 常驻泵 | `Kernel/HAL/X64/Drivers/Bus/XHCI/Bluetooth/` | 扩现有；新泵文件名写全（如 `HostPump.c`） |
| LE / SMP / ATT / HOGP | 同目录下分子文件；单文件 ≤300 行 | 不另起 `Drivers/Bluetooth/` 逃板债 |
| Hal 门面 | `Kernel/Include/Hal/HalBluetooth.h` + `Kernel/HAL/X64/Hal/HalBluetooth.c` | 按刀扩 API；禁空 stub 谎 Ready |
| 壳 | `Kernel/Services/Shell/Commands/Bluetooth.c` | 加 `lescan` / `mouse` / `disconnect` |
| 输入后端 | 与现 `INPUT_BACKEND` 同形新建 | **不**占 USB 鼠槽 |

Pi/Duo `Board/*/Bluetooth/`：**本柱不改**（另柱再接上层）。

### 壳命令面（钉死）

| 命令 | 自刀 | 行为 |
|---|---|---|
| `bt` / `bt info` | 已有 | Ready / 名 / 可见性 |
| `bt scan` | 已有 | **经典** Inquiry（保留；本鼠扫不到属正常） |
| `bt visible` / `bt hide` | 已有 | 本机可被发现 |
| `bt lescan` | BTM-3 | LE 扫；列表序号+地址+名 |
| `bt mouse <addr\|#>` | BTM-8 | 一键：连 → SMP → HOGP → 挂后端 |
| `bt disconnect` | BTM-10 | 断当前 LE 鼠连接 |

### 双指针策略（钉死）

**USB 鼠与 BT 鼠并存**；两者均可 `MousePush`；**后到报告驱动光标**（与多 USB 鼠同形）。  
不卸 USB 后端、不互抢槽位。手测须同时晃有线鼠确认未冻。

---

## 5. PR 拆分（建议序）

> 一刀一验收；估为课堂量级。序内依赖自上而下。  
> 命名：口令正名「蓝牙鼠」；登记 `BluetoothMouse`；PR 前缀 **`PR-BTM-`**。

### A. 主机地基（经典/LE 共用）

| PR | 内容 | 验收 | 估 |
|---|---|---|---|
| **PR-BTM-0** | 规格钉死：靶鼠=初代 Master；链路=BLE HOGP；手测清单进本档 §7 | 文档齐；可无代码 | 0.5 |
| **PR-BTM-1** | HCI **常驻泵**：事件缓冲队列 + ACL IN 投递/组装；`Number of Completed Packets`；泵切片调 `ServiceHidCompletions` | 串口 `bt: host pump`；有线鼠拖动不卡死；经典 `bt scan` 不回归 | 2–3 |
| **PR-BTM-2** | ACL OUT + `Read Buffer Size` 流控；HCI 命令可与泵并行（禁「全局单包自旋」） | 能发任意 HCI/ACL 测试包并收回事件；有线鼠仍优先 | 1–2 |

### B. LE 连接与配对

| PR | 内容 | 验收 | 估 |
|---|---|---|---|
| **PR-BTM-3** | LE 扫描：`LE Set Scan Parameters/Enable`；解析 **LE Advertising Report**；壳 `bt lescan` | 初代 Master 可被发现态时列表见地址/名（或 Logitech 厂商段） | 2–3 |
| **PR-BTM-4** | `LE Create Connection` → Connection Complete；断开/超时有日志 | 选定地址后 `bt: le connected handle=…` | 1–2 |
| **PR-BTM-5** | **SMP Just Works**（NoInputNoOutput）；链路加密完成 | `bt: smp ok`；无显示确认 UI | 2–4 |

### C. HOGP → 光标

| PR | 内容 | 验收 | 估 |
|---|---|---|---|
| **PR-BTM-6** | ATT 客户端最小：MTU、Read/Write、Notify 注册；发现 **HID Service** | 日志见 HID service + Report 特征句柄 | 2–3 |
| **PR-BTM-7** | 读 **Report Map**（可先存原始）；订阅读 HID Input Report Notify | 通知字节流进缓冲；串口可 hexdump 首包 | 2–3 |
| **PR-BTM-8** | 报告 → `HAL_MOUSE_REPORT`；**第二 `INPUT_BACKEND`**；壳 `bt mouse <addr\|#>` 一键连 | Gui 光标跟手；左键可点；有线鼠仍可用（策略：BT 优先或并存不互抢，钉一种） | 2–3 |

### D. Master 适配与收口

| PR | 内容 | 验收 | 估 |
|---|---|---|---|
| **PR-BTM-9** | 初代 Master 报告形状适配（Boot 不足则按 Map/实测偏移解 dx/dy/buttons）；**不做**全 HID++ | 无接收器、仅蓝牙通道手测绿 | 1–3 |
| **PR-BTM-10** | 断线重连（同会话）；`bt mouse` / `bt disconnect`；README / 本档勾绿；鼠优先自查表 | 拔开关蓝牙通道再开可恢复；文档收口 | 1 |

**合计粗估**：**约 12–18 刀课堂量**（SMP + Report Map 风险最大；若 HCI 固件 LE 不稳再加眼睛刀）。

### 明确不做（本柱红线）

| 项 | 原因 |
|---|---|
| 经典 HIDP / SDP | 本鼠不走 |
| Unifying / Bolt 协议 | 接收器已丢失；另柱 |
| 持久链路密钥 / IRK 落盘产品化 | 可后补；MVP 每会话重配 |
| 多键手势 / 横向滚轮完整映射 | 后补；MVP 三键+位移 |
| Pi / Duo 蓝牙鼠 | 无 ACL/LE 课轨 |
| A2DP / 键盘 BLE | 另柱 |

---

## 6. 硬约束

1. **标准优先**：Bluetooth Core（HCI LE）、SMP、ATT/GATT、HOGP；对照 Linux `hid-logitech-hidpp` 仅作 **Master=BLE** 证据，禁止抄内核私货当规格。  
2. **鼠优先**：任何扫/连/通知循环必须切片 `ServiceHidCompletions`；禁止整秒 `IrqSave` / 堵 xHCI。  
3. **有注释先看**：`bt-defer`、USB HID keep、Visible「不做 HID-over-BT」——本柱**更新注释**说明 HOGP 已开，禁止默默删旧注释。  
4. **命名写全**；单文件 ≤300 行（HCI / SMP / ATT / HOGP 分目录）。  
5. **Hal 契约**：未 Ready / 未加密不得冒充已连鼠；壳失败打短因。  
6. **双指针**：见 §4「双指针策略」——并存、后到驱动光标。

---

## 7. 手测硬件与验收清单

| 项 | 钉死 |
|---|---|
| 主机 | NUC（板载 `8087:0A2B` 或同轨 USB HCI） |
| 鼠 | **罗技 MX Master 初代**；底面通道切到 **Bluetooth**；长按连接键进入可被发现 |
| 对照 | Linux `bluetoothctl` 能 LE 扫到同鼠（排「鼠坏/通道错」） |
| 有线 | 手测全程保留 USB 键鼠，用于回归「不饿死」 |
| 接收器 | **不依赖**（已丢失） |

**柱通验收节拍（BTM-8+）**

1. 桌面已露光标（BT 已 `bt-defer` 初始化）。  
2. `bt info` → ready。  
3. 鼠进配对态 → `bt lescan` 见设备。  
4. `bt mouse <n>` → 串口 `smp ok` / `hogp ok`。  
5. 拖动 / 左键；同时晃 USB 鼠确认未冻。

**`PR-BTM-0` 验收（本文档刀 · 无代码）**

- [x] 靶鼠 = 初代 MX Master；链路 = BLE + HOGP；配对 = Just Works 本会话  
- [x] 课机 = NUC USB HCI；Pi/Duo / Unifying / 经典 HIDP 进红线  
- [x] PR 序 `0…10`、壳命令面、落点目录、双指针策略已钉  
- [x] 手测硬件表 + 通验收节拍进本 §7  
- [x] 活档 ★ 推至 `PR-BTM-1`

**`PR-BTM-1` 验收**

- [x] `log` 见 `bt: host pump`（Ready 后）  
- [x] 有线鼠拖动不卡  
- [x] 经典 `bt scan` 不回归  

**`PR-BTM-2` 验收**

- [x] `log` 见 `bt: acl mtu=` / `credit=`  
- [x] `bt acltest` → `acl out ok` / `acltest ok`  
- [x] 有线鼠仍优先（测试窗不占 Exclusive）  

---

## 8. 风险与否证

| 风险 | 否证 / 退出 |
|---|---|
| 板载 HCI LE 需厂商补丁才稳 | BTM-3 扫不到而 Linux 同机看得到 → 先开「固件/补丁眼睛」刀，不盲写 SMP |
| Master 仅厂商报告、无标准 HID Input | BTM-7 有 Notify 但无位移 → BTM-9 对表 Report Map；仍无则记录红并停柱 |
| 配对要 Passkey | Just Works 失败 → 柱暂停（无显示 UI 不做数字比对） |
| 与 iwl/有线抢 CPU | 泵限频；关联 Wi‑Fi 时仍以键鼠优先 |

---

## 9. 修订

| 日期 | 说明 |
| ---- | ---- |
| 2026-11-09 | **`PR-BTM-2` 收**：`HostPumpAclOut` + `bt acltest`；★→`PR-BTM-3` |
| 2026-11-09 | **`PR-BTM-1` 收**：`HostPump`/`HostPumpAclIn`；NUC `bt: host pump` + 有线顺 + `bt scan`；★→`PR-BTM-2` |
| 2026-11-09 | **`PR-BTM-0` 收**：规格钉死（落点/壳命令/双指针/§7 勾选）；★→`PR-BTM-1` |
| 2026-11-09 | 排期迁 [`路线图1109.md`](../路线图1109.md)；候补推星排队（`PR-BTM-0…10`） |
| 2026-11-08 | 立项：初代 MX Master、无接收器 → BLE HOGP；拆 `PR-BTM-0…10`；不进 ★ |
