# 11 · DUO WiFi 失败记录（2026-10-31 暂停）

> **用途**：以后任何人（包括未来的我）接手 DUO WiFi 时，**先读这一份**，不要重复已经证伪的路。
> **原则**：每条都写「假设 → 做法 → 真机结果 → 结论」，不写"试过了"这种无法复查的话。
> 所有结果都来自真机（Milk-V Duo S + AIC8800D80 + 板上 SD 卡），不是纸上推演。

---

## 0 · 一句话结论

**关联能成功，但关联之后固件不再向主机上送任何数据帧（含 EAPOL M1）。**
八条常见成因逐条实测排除；剩下只有「空口抓包」或「固件内部轨迹」两条路。

---

## 1 · 已经打通的部分（5 / 8 层）

| 层 | 状态 | 判据 |
|---|---|---|
| SDIO 枚举 | ✅ | 识别 AIC8800D80，功能 0/1 就绪 |
| 固件下载 | ✅ | `patch applied 101/101, fw state=done` |
| 无线电配置 | ✅ | `ME_CONFIG` / `ME_CHAN_CONFIG` / `MM_START` 三条 CFM |
| 扫描 | ✅ | 多轮合并出真实 SSID（Apple / Sense / TP-LINK_YY），`rounds: 7,3,3` 证明每轮真扫 |
| 关联 | ✅ | `SM_CONNECT_IND status_code=0x0000`，BSSID/信道/RSSI 正确 |
| **四次握手** | ❌ | **收不到 M1**（本文件主题） |
| 数据通路 | ⬜ | 未开始（依赖握手） |
| lwIP / DHCP | ⬜ | 未开始 |

---

## 2 · 失败的八条假设（逐条真机证伪）

### 假设 1：控制端口开得太晚

**理由**：AP 在关联完成的瞬间就发 M1；我们从收到 IND 到开控制端口之间要打三四行串口（115200 下每行 5–10 ms），怀疑 M1 到达时端口还关着被丢。

**做法**：`IND` 一到、解析出 `ap_idx` 就立刻发 `ME_SET_CONTROL_PORT_REQ(0x1404)`，**所有打印推迟到之后**。

**结果**：
```
WiFi: control port req: sta_idx=0 open=1
WiFi: SM_CONNECT_IND: status_code=0x0000
WiFi: control port open (vif=0 ap=0)
→ 仍然只有 beacon，收不到 M1
```

**结论**：❌ 无效。另外**发现一个反证**：厂商 `rwnx_send_me_set_control_port_req` 的唯一调用点在 `AUTHORIZED` 分支（`rwnx_main.c:4606-4610`），而 `AUTHORIZED` 是**四次握手之后**才置的 —— 说明厂商收到 M1 时控制端口其实是**关着**的。**"必须开控制端口才能收 EAPOL"这个前提本身就不成立。**

**附带认知**：`ME_SET_CONTROL_PORT_CFM(0x1405)` **没有状态字段**（`lmac_msg.h:1757-1763` 只有 req 结构）⇒ 收到 CFM 只证明消息被吃下，**不证明端口真开了**。

---

### 假设 2：`ctrl_port_ethertype` 字节序反了

**理由**：字段注释写明 network endianness（`lmac_msg.h:2224`），厂商传 `__be16` 的 0x888E（内存 `88 8E`），我们按 `UINT16 0x888E` 小端写进内存变成 `8E 88`。若固件靠这个字段认 EAPOL，就永远匹配不上。

**做法**：常量改 `0x888E → 0x8E88`（内存字节 `88 8E`）。

**结果**：`sm_connect_req … eapol=0x8e88`，仍然只有 beacon。

**结论**：❌ 无效。**但这条改动保留**——它在语义上更接近厂商（内存 `88 8E`），值得留在代码里。

---

### 假设 3：固件用的 STA MAC 与我们 ADD_IF 的不一致

**理由**：厂商恒等式是「`MM_ADD_IF` 的 MAC == 固件 efuse MAC」（`rwnx_main.c:7072/7086/3050-3052/1039`）。我们 `ADD_IF` 用的是硬编码 `02:00:00:00:00:01`。若固件单播过滤按 efuse MAC 做，单播整片丢、广播/管理帧照过 —— 与现象吻合。

**做法**：新增只读查询 `MM_GET_MAC_ADDR_REQ(0x73) → CFM(0x74)`（id 由厂商枚举逐项数出，编译验证）。

**结果**：
```
WiFi: fw mac=00:00:00:00:00:00 (add_if uses 02:00:00:00:00:01)
```

**结论**：❌ 假设不成立 —— 这块板子的 efuse 里**没有 MAC**（全 0）。查询代码保留为诊断。

---

### 假设 4：缺 `MM_SET_STACK_START_REQ`

**理由**：厂商在**固件下载完之后的第一条**就发它（`rwnx_main.c:6947`，早于 `MM_RESET`），语义是"主机协议栈已启动"；`aicbsp`（BSP 模式）发 0、`aic8800_fdrv` 发 1。若固件以为自己在 BSP 模式（没有主机协议栈），就可能不把数据交给主机。

**做法**：下载完成后补发 `0x7B {1,0,CO_BIT(5),0}`，等 CFM `0x7C`。

**结果**：
```
WiFi: stack start ok (5g=01 vendor=00)     ← 固件接受了
→ 仍然只有 beacon
```

**结论**：❌ 无效。**补发保留**（它确实是厂商 init 序列的一部分）。

---

### 假设 5：扫描状态残留

**理由**：我们是"连打 3 轮扫描后直接 join"，厂商在 connect 前若仍在扫会 abort（`rwnx_main.c:3575-3581`）。若固件残留扫描状态，只上送 beacon 是典型表现。

**做法**：join 前发 `SCANU_CANCEL_REQ(0x100A)`，等 `0x100B`。

**结果**：**固件不回 CFM**（`join: scan cancel cfm timeout`），继续 join 也不报错，但仍收不到 M1。

**结论**：❌ 无效（且固件根本不响应取消）。

---

### 假设 6：顺序不对——没先 `MM_RESET` 就配置

**理由**：厂商顺序是 `stack_start → GET_MAC → MM_RESET → ME_CONFIG → CHAN → START`；我们是 `download → ME_CONFIG → CHAN → START`，中间没有 reset。

**做法**：`write confirm → mmreset → bringup → scan → join`。

**结果**：
```
WiFi: mm reset ok
WiFi: mm reset: dropped vif/scan/join caches
WiFi: add if cfm ok (vif=0)          ← 这次是新建 vif
WiFi: join cfm ok (status=0x00)
WiFi: SM_CONNECT_IND: status_code=0x0000
→ 仍然只有 beacon
```

**结论**：❌ 无效。**顺带修好一个真 bug**：`mmreset` 之后不清缓存会导致后续 join 恒失败（`CFM status=0x04`）——这条已修（见 §4）。

---

### 假设 7：卡进入睡眠态

**理由**：`fn1 0x01` bit4 是"SDIO 活着"标志；若卡睡了，寄存器能读、数据不给 —— 与现象吻合。

**做法**：实现 D80 的电源/中断状态机（`fn1 0x02←0x11` 唤醒握手、`fn1 0x04` bit7 应答），并在超时时打印全部寄存器现场。

**结果**：
```
WiFi: sdio state: fn1 01=10 04=00 … 
WiFi: sdio verdict: awake=1 devirq=0 blocks=0
```

**结论**：❌ 假设不成立 —— **卡一直是醒的**。状态机代码保留（无害且必要）。

---

### 假设 8：一次读回多帧，我们只解第一帧

**理由**：厂商用 `while (aicwf_another_ptk())` 逐帧解析（`aicwf_txrxif.c:377`）；我们只解第一帧。一个 beacon 与一个 M1 可能同块，M1 会被整帧丢掉。

**做法**：实现 `EapolFrameSpan` / `EapolDispatchBlock` 逐帧解析（数据帧 `align4(60+pkt_len)`、配置帧 `align4(pkt_len)+4`）。

**结果**：
```
WiFi: eapol: aggregate in one block=02 read=0200
```

**结论**：✅ 修好了一个真 bug（确实会丢帧），**但它不是 M1 收不到的原因** —— 修好之后仍然只有 beacon。

---

## 3 · 当前确定的事实（可直接引用）

1. 关联成功：`SM_CONNECT_IND status_code=0x0000`，`bssid=1c:36:bb:04:fd:80 freq=2447 ch=8`；
2. 控制端口消息被接受（0x1405 到达，但**该 CFM 无状态字段**）；
3. 固件醒着（`awake=1`）、无待收块（`blocks=0`）；
4. **3 秒窗口内除 beacon 外零数据帧**；连 15 秒长轮询也是 0 帧；
5. 同一 AP、同一张卡，**eMMC Linux 一关联就收到 M1**（`RX EAPOL from …`，len=99，key_info=0x008a）⇒ **AP 会发、固件能交**；
6. 厂商 STA 路径在 `SM_CONNECT` 到第一次 EAPOL 之间**一条 host→fw 消息都不发**（逐行核过）⇒「漏发一条消息」这个框架不成立。

---

## 4 · 这一阶段顺带修好的真 bug（都保留）

| 内容 | 判据 |
|---|---|
| 聚合帧逐帧解析 | `aggregate in one block=02` |
| `mmreset` 后清 vif/扫描/join 缓存 | `dropped vif/scan/join caches` → `add if cfm ok (vif=0)` |
| D80 电源/中断状态机 + 寄存器现场 | `sdio wake` / `sdio state` / `sdio verdict` |
| 只读 `MM_GET_MAC_ADDR` 诊断 | `fw mac=` |
| 补发 `MM_SET_STACK_START` | `stack start ok (5g=01)` |
| EAPOL ethertype 按网络序写 | `eapol=0x8e88` |

---

## 5 · 剩下两条路（都不是"再猜一次"）

| 路 | 需要什么 | 能得到什么 |
|---|---|---|
| **空口抓包** | 第二台设备做 monitor 模式，同信道，覆盖 join 的 ~3 秒 | ① 有没有 M1、DA 是不是我们的 MAC；② 有没有重传；③ 我们的 assoc 请求到底带了什么 IE |
| **固件内部轨迹** | AIC 固件的调试口/逆向 | 固件为什么不 upload |

**空口抓包的关键判别**：

- **有 M1 且 DA = 我们的 MAC** ⇒ 固件丢的（回到主机侧）；
- **有 M1 但 DA 不同** ⇒ 固件 assoc 用的 MAC 与我们以为的不同；
- **完全没有 M1** ⇒ 问题在 AP 侧/关联内容（要回头看 RSN IE 是否真的上了空口）。

---

## 6 · 给接手者的三条建议

1. **不要**再从"漏发某条消息"入手 —— 厂商那段窗口里零消息（§3 第 6 条）；
2. **不要**再调控制端口/睡眠/聚合 —— 都已经实测排除（§2 假设 1/7/8）；
3. **先做空口抓包**再动代码。盲改的成本是：每轮 = 一次刷卡 + 一次上电 + 一次完整 bringup。

---

## 7 · 参考：本阶段用过的真机命令序列

```
wififw write confirm        # 固件下载（含补发的 stack_start）
wififw ver                  # 版本 + fw mac
wififw bringup              # ME_CONFIG / ME_CHAN_CONFIG / MM_START
wififw scan                 # 多轮合并
wififw join Apple <密码>     # 关联 + 控制端口 + 四次握手（卡在 M1）
wififw eapol poll 3000      # 单独轮询数据帧（带 polls/empty 统计）
wififw mmreset              # 复位（会清缓存）
```

**注意**：`mmreset` 之后必须重新 `scan` 才能 `join`（缓存已清空）。
