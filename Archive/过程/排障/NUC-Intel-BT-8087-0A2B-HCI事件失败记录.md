# NUC · Intel BT 8087:0A2B · HCI 事件失败记录（PR-BTM-1）

> **日期**：2026-11-09  
> **用途**：接手蓝牙鼠 / HostPump / 固件灌装前**先读这一份**，勿重复已证伪路。  
> **原则**：每条「假设 → 做法 → 真机结果 → 结论」；结果均来自 NUC 串口 + `D:RUNTIME-BOOTDISK.LOG`。  
> **活 ★**：[`路线图1109.md`](../../路线图1109.md) → **`PR-BTM-1`**（HCI 常驻泵；**HCI Ready 已绿，泵代码尚未挂回**）。  
> **柱规格**：[`柱规格/蓝牙鼠-罗技MXMaster.md`](../../柱规格/蓝牙鼠-罗技MXMaster.md)。  
> **相关 commit**：`26ad45e1`（`.new` 热更 + Intel `.sfi` 预载 / FC09 Bulk）；`2f9c19b0`（TcpSocketSend 有线/Wi‑Fi 分时限，已仓推）。

---

## 0 · 一句话结论

**冷启后 HCI Ready 已绿。** 串口：`bt: ready name=Blocks addr=94:B8:6D:F4:C5:EE visible`。  
RUNTIME：`FC05` → bootloader `fw_variant=0x06` → `.sfi` 分片灌完 → Reset/BD_ADDR/可发现。

此前软 reboot 上 Interrupt 哑火（`ep-st=1`、`deq==arm`、`unmatched=0`）是 **芯片事件通路未硬复位**，不是 ConfigEP 配错。  
`PORTSC.PP` 循环救不了；**拔电源冷启**才回到「能回 CC」状态。

**PR-BTM-1 常驻泵仍未挂回树**（曾挪走以免抢 Arm/Wait）。Ready 是泵的前置，不是泵本身。

---

## 1 · 已经打通的部分

| 层 | 状态 | 判据 |
|---|---|---|
| 根口认 HCI | ✅ | survey 口 `08` CCS；`ForceUntilPed` → `enabled`；`vid=0x8087 pid=0x0A2B class=0xE0` |
| 配置 / 端点 | ✅ | `cfg ok iface=0x01 event=0x81 acl-in=0x82 acl-out=0x02`；`event-dci=0x03` |
| EP0 控制传输 | ✅ | GetDescriptor / SetConfig / class HCI command **SendRaw 不因 Control 失败返回** |
| 固件文件在盘 | ✅ | `Intel .sfi bytes=0x0008E99C`（`FW/ibt-12-16.sfi`）；HotSend `.new` 覆核绿 |
| 有线热更 | ✅ | HotSend wired；`TcpSocketSend` 有线 100ms / Wi‑Fi 2s（`2f9c19b0`） |
| **Interrupt 收 HCI 事件** | ✅ 冷启后 | `FC05` 有 CC；软 reboot 曾 `xfer-cc=0` |
| Intel 固件灌完 → operational | ✅ 冷启后 | `fw download begin` → `fragments=0x0100…` → Ready |
| Ready / `bt info` | ✅ | `bt: ready name=Blocks addr=94:B8:6D:F4:C5:EE visible` |
| HostPump | ❌ | 代码不在树；★ 仍是泵 |

手测口令：`HotSend serve` → 主机 `hotsend` → `reboot` → `bt info` → `print D:RUNTIME-BOOTDISK.LOG`（串口长命令须 **echo 校验逐字**，否则易吞字母）。

---

## 2 · 现象基线（2026-11-09 反复）

```
Boot: Bluetooth try port=0x08
Boot: Bluetooth portsc=0x000006E1          ← 初扫常 CCS+!PED
Boot: Bluetooth enabled port=0x08
Boot: Bluetooth HCI vid=0x8087 pid=0x0A2B …
Boot: Bluetooth ep speed=0x01              ← PortSpeed 报 FS
 event-dci=0x03
 interval=0x03                             ← FsInterval 路径
Boot: Bluetooth cfg ok iface=0x01 event=0x81 …
Boot: Bluetooth HCI cmd timeout op=0x0C03
 xfer-cc=0x00                              ← EventDone 从未置；非坏完成码
Boot: Bluetooth HCI Reset send/event fail
Boot: Bluetooth Intel .sfi bytes=0x0008E99C
Boot: Bluetooth HCI cmd timeout op=0xFC05
 xfer-cc=0x00
```

对照（**曾绿过一轮**，同口同 vid/pid，同日更早）：

```
Boot: Bluetooth cfg ok iface=0x01
Boot: Bluetooth CC opcode=0x0C03
Boot: Bluetooth HCI Reset bad status       ← Interrupt 有 CC，status≠0（bootloader 预期）
Boot: Bluetooth Intel .sfi bytes=0x0008E99C
Boot: Bluetooth Intel fw download begin
Boot: Bluetooth HCI cmd timeout op=0xFC09  ← 卡在 secure-send，不是「完全无事件」
```

**差分解读**：从「有 CC、卡 FC09」退化成「连 Reset/FC05 都无 CC」。软 `reboot` **不能**当成 Intel 硬复位；内部 BT 轨是否跟 `PORTSC.PP` 掉电未证实。

---

## 3 · 失败假设（逐条真机）

### 假设 1：HostPump 抢 EventDone / 过早 Start

**理由**：BTM-1 常驻泵与单缓冲 Arm/Wait 共用 `gBluetoothEventBuffer`；若 `HostPumpStart` 在 Reset 前跑，或 `gBluetoothHostPumpActive` 非 0，ArmEvent 会跳过投 Interrupt TRB → 必 `xfer-cc=0`。

**做法**：
- 推迟 `HostPumpStart` 到 Ready 后（`Info.c`）；Reset 前 `HostPumpStop`。
- Active 改魔数 `BLUETOOTH_HOST_PUMP_ON`（`'BTM1'`），禁止「非 0 即真」（防 BSS 垃圾）。
- 一度整树 Bluetooth/Irq **回退到 `26ad45e1` 等价**（移走 `HostPump.c`），排除泵代码。

**结果**：回退后仍 `op=0x0C03` / `xfer-cc=0`。与「曾 bad status」同树形仍复现无事件。

**结论**：❌ **HostPump 不是当前无 CC 的充分原因**（早期「pump 打在 Reset 前」确实害过，已改；但今日主红仍在）。泵代码可后挂，先把事件链救活。

---

### 假设 2：ConfigEP 后 dequeue 与 HC 失步（对表 HID Sync）

**理由**：HID 重臂前走 `SyncIntrDequeue`（Stop→Init→SetTrDeq）。BT 只在 ConfigEP 写 dequeue 后直接 Enqueue。

**做法**：`ConfigureHciEndpoints` 成功后对 Event DCI 调 `SyncIntrDequeue`。

**结果**：
```
Boot: XHCI xfer s=11 e=03 c=27    ← CC_STOPPED_LEN，Stop EP 副作用；Configured 尚未置 1，Match 未吃
… cfg ok …
仍 HCI cmd timeout op=0x0C03 xfer-cc=0
```

**结论**：❌ 无效。**已撤 Sync**（噪声大、无改善）。首次 ConfigEP 后直接 Arm 与历史绿路径一致，不是差分点。

---

### 假设 3：芯片卡死，需口电源循环（对表 btusb cmd_timeout GPIO）

**理由**：Linux 对 Intel 多次 cmd timeout 可走 reset GPIO；我们无 GPIO，试 `PORTSC.PP` 掉电再上电 + `ForceUntilPed`。

**做法**：`BluetoothAddressRoot` 在 Force 前：`PP=0` stall 150ms → `PP=1` stall 100ms → ForceUntilPed；打 `pp-cycle` / `post-ped` 日志。

**结果**：
```
Boot: Bluetooth pp-cycle portsc=0x000206E1
Boot: Bluetooth post-ped portsc=0x00200603
 spd=0x01
… cfg ok …
仍 timeout op=0x0C03 / FC05，xfer-cc=0
```

**结论**：❌ 对「恢复 Interrupt CC」无效。`PP` 是否真断内部 BT 供电**未证实**；USB 总线复位（Force）也未拉回「bad status」时代。  
**保留**：作为「软 reboot 不够」的实验痕迹；勿当成已验证硬复位。

---

### 假设 4：复合 iface 未 SET_INTERFACE

**理由**：`iface=0x01`；EnumeratePort HID 对复合会 `SetInterface(alt=0)`。怀疑 HCI iface 未显式激活导致中断不吐。

**做法**：`SetConfig` 后 `SetInterface(Interface, 0)`，失败则 cfg 失败返回。

**结果**：`cfg ok`（无 `setiface fail`）；随后仍 `FC05` / `xfer-cc=0`。

**结论**：❌ 对恢复 CC **无效**。SET_INTERFACE 可留作惯例，不是根因。

---

### 假设 5：标准 HCI_Reset 在 bootloader 有害 / 空耗

**理由**：Intel NEW 在 bootloader 下 Reset 常 status≠0；今日 Reset 还 2s 空等无 CC。对表 btusb：可先 `0xFC05` / 灌片。

**做法**：`8087:0A2B` 分支：**跳过先 Reset**，直接 `BluetoothIntelFirmwareLoad()`（内含 FC05）。

**结果**：日志不再先打 `op=0x0C03`；直接 `.sfi` → `timeout op=0xFC05` + `ep-st=0x01` + `unmatched=0`。

**结论**：✅ 作为路径整理**保留**（少烧 2s）；❌ **不治** Interrupt 无事件。

---

### 假设 8：空 ACL IN 配进 ConfigEP 弄死 Interrupt

**理由**：bootloader 阶段 Add 了从未门铃的 Bulk IN。

**做法**：ConfigEP **只 Add Event + ACL OUT**；`gBluetoothAclInDci=0`。

**结果**：仍 `FC05` timeout；`ep-st=0x01` `unmatched=0` `event-mps=0x0040`。

**结论**：❌ 无效。ACL IN 不是当前无 CC 的原因。

---

### 取证拐点（2026-11-09 · `e77ea4fe` / `7c647566`）

| 字段 | 值 | 含义 |
|---|---|---|
| `ep-st` | **`0x01` Running** | 门铃后 HC 在跑 Interrupt TD |
| `unmatched` | **`0`** | 事件环上**没有**漏接的 Transfer Event |
| `xfer-cc` | `0` | `EventDone` 从未置 |
| `event-mps` / `mps` | `0x40` | 武装长度 64，非 0 |
| `deq` / `arm` | **相同** `0x027A3100` | Output Context dequeue **未推进**＝TRB 未被完成消费 |
| Control | 无 `HCI control fail` | EP0 class OUT **成功**（设备 ACK 了命令字节） |

**判定（钉死）**：

```
主机：Arm → Doorbell → EP Running → dequeue 停在武装环
设备：Control 收了 FC05，Interrupt IN 不吐 → 无 Transfer Event → 无 CC
```

不是 Match 漏判、不是 EP Disabled/Stopped、不是 MPS=0、不是 ACL IN 连坐。  
与「曾有 Reset bad status（有 CC）」对比 → **芯片 HCI 事件通路当前哑火**；软 `reboot` / `PORTSC.PP` 循环未能恢复。  
下一刀应优先：**整机断电冷启**（或平台 BT_REG_ON/GPIO 硬复位，本树暂无）验证能否回到「bad status」；再谈 FC09/HostPump。

---

### 假设 6：FC09 须按 MPS 切 TRB_CHAIN

**理由**：单 TRB 超 ACL MPS 时部分主控「TRB 完、设备未吃满」→ 无 CC。

**做法**：`HciSend` Bulk OUT 按 `AclOutMaxPacketSize` 切段，中间 `TRB_CHAIN`，末 `TRB_IOC`。

**结果**：历史在「Reset 已有 CC」时仍 FC09 timeout；今日连 Reset 无 CC，CHAIN **未再验证**。

**结论**：⬜ 对 FC09 可能仍需要；**当前阻塞点在更早的 Interrupt**。CHAIN 定义（`TRB_CHAIN=1<<4`）留待事件恢复后再开。

---

### 假设 7：speed=FS（0x01）配错导致中断永不调度

**观察**：`post-ped portsc=0x00200603`，`ep speed=0x01`，`interval=0x03`（`FsInterval`）。口 08 初扫 `0x6E1` 时 Speed 字段本就不可靠；PED 后仍报 1。

**做法**：尚未强改 Speed=HS（禁止无证据猜速度）。

**结果**：—  

**结论**：⬜ **未证伪、未证实**。EP0 在同一 Speed 下能枚举，说明 FS 槽上下文至少能跑控制；是否拖死 Interrupt 待对表 xHCI + 描述符 bInterval / 是否应上 HS。  
**禁止**：无 PORTSC/描述符证据就写死 `Speed=3`。

---

## 4 · 已排除的「瞎猜」清单（勿再开）

| 别再做 | 原因 |
|---|---|
| 把 HostPump 当无 CC 的唯一根因反复拧 | 回退 `26ad45e1` 形仍无 CC |
| ConfigEP 后无证据乱 SyncIntrDequeue | 只引入 `c=27` 噪声 |
| 缩短/加长 Wait 碰运气 | 标准优先；2s 窗内零完成事件 |
| 默认 `ksend.py` 当热更主路 | 口令 **核传** = HotSend |
| 未绿就仓交 HostPump / 推 ★ | 协作：刀做只认验收；BTM-1 手测未过 |

---

## 5 · 代码地图（当前相关）

| 路径 | 职责 |
|---|---|
| `XHCI/Bluetooth/Address.c` | 口 08 Force /（实验）PP 循环 / FinishProbe→Reset |
| `XHCI/Bluetooth/Configure.c` | 解析 HCI EP、SetConfig、SetInterface、ConfigEP |
| `XHCI/Bluetooth/Hci.c` | Arm/Wait、MatchTransfer、Command、Reset/Intel 分支 |
| `XHCI/Bluetooth/HciSend.c` | Control SendRaw；FC09 Bulk（可 CHAIN） |
| `XHCI/Bluetooth/IntelFirmware*.c` | `.sfi` 读盘、FC05/FC09/FC01 |
| `XHCI/Bluetooth/HostPump.c` | **曾实现，现从树挪走备份** `/tmp/HostPump.c.btm1.bak`；Ready 前勿 Start |
| `XHCI/EventTransfer.c` | Transfer 分发 → `BluetoothMatchTransfer` |
| `Runtime/.../FW/ibt-12-16.sfi` | WsP 固件 |

---

## 6 · 下一步（未做 / 建议序）

1. **挂回 HostPump**：仅 `Bluetooth Ready` 之后 `Start`；bootloader 单缓冲 Arm/Wait；Active 用魔数。  
2. 泵切片调 `ServiceHidCompletions`；验收串口 `bt: host pump` + 有线鼠不卡。  
3. 软 reboot 后再哑火时：**先冷启**，勿再拧 ConfigEP/Sync/PP。  
4. FC09 事件：冷启后 Interrupt 路径已能灌片；Bulk IN 收 FC09 是 btusb 对照，作加固而非当前阻塞。

---

## 7 · 教训（给 BTM-2… / 回灌）

1. **Control 绿 ≠ HCI 事件绿**；验收必须看到 CC/`xfer-cc≠0` 或明确完成码。  
2. **「曾经 bad status」是金标准对照**；退化后先问「谁弄死了 Interrupt」，再问固件算法。  
3. **软 reboot ≠ Intel 芯片复位**；PP 循环也未证明掉电。  
4. HostPump 是 Ready 后的形态；**bootloader 阶段保持单缓冲 Arm/Wait**，且 Active 勿用裸非 0。  
5. 长串口命令要 echo 校验（`RUNTIME-BOODISK` 一类吞字会假「cat: not found」）。

---

## 8 · 关联

- 热更 / 大文件覆写：`26ad45e1` 说明；口令 **核传**。  
- 蓝牙鼠柱：`路线图1109.md` ★=`PR-BTM-1`。  
- 姊妹「中断武装无事件」：[`MIX2-USB键盘试错历程.md`](MIX2-USB键盘试错历程.md)（`l0`）；本档是 **xHCI + Intel USB HCI**，勿混手机 DWC3。
