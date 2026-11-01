# 25 · DUO WiFi 挂通用网 — DeepSeek 执行包

> **给执行者（DeepSeek）**：按 ★ 一刀做；一次一变量；未过判据 **不 TG / 不 push**。  
> **给评审（Cursor）**：对表 §8 红线；填本刀表末行 **评审**。  
> **权威 ★ / 刀序摘要**：[`Archive/路线图1101.md`](../Archive/路线图1101.md)#dwifi-netif（日档文首 ★ 为准；本文是可独立开干的细包）。  
> **Pi**：1031 wifi-7c2…7e **已绿，本柱不要改 Pi**。

### 协作闭环（DS ↔ Cursor · 2026-11-01 起）

| 谁 | 节奏 | 动作 |
|---|---|---|
| **DeepSeek** | 做完一刀 | **只填本刀「进度」**（串口原文 / 文件 / 自查）；**此时不要 TG/TS** |
| **DeepSeek** | 每 ~10 分钟 | ① 读 [`Archive/协作信箱/TO-DS.md`](../Archive/协作信箱/TO-DS.md)（切换/口令以它为准）② 读**当前执行包**本刀 **「评审」**：若 **`✅ 全绿`** → 才 `TG`+`TS`，推 ★，再 `JX`；本柱刀做完见下行「收官切柱」 |
| **Cursor** | 每 ~10 分钟 | 读**当前执行包**：进度有更新 → 抽查 → 只改该刀表末 **「评审」**；若本柱收官则 **切下一包并写 TO-DS 通知** |
| **评审行格式** | — | 开头必须：**`✅ 全绿`** 或 **`🔧 未收官`** / **`❌ 红`**。只有 **全绿** 才允许 DS TG TS JX。**协作/文档有变更时，评审末尾加「给 DS」短节**（勿只改文首让 DS 漏读） |
| **收官切柱** | Docs/25 | 当 netif 柱验收完（netif-5 全绿且 1101 本柱 ★ 推空，或用户点名收官）→ **下一包 = [`Docs/24`](24-板端蓝牙-Pi与Duo执行包.md)**；Cursor 更新 1101 ★=`bt-pi-0`，并 **必写 TO-DS「切换通知」** |
| **禁止** | — | 非全绿 TG/TS；Cursor 替 DS 填进度；**未切柱时**同会话改 Docs/24；切到 24 后勿再改 Wifi/netif |

> **当前模式**：JX → 填进度 → 评审 →（仅全绿）TG TS → JX。  
> **当前执行包**：本文 Docs/25。**排队下一包**：Docs/24 蓝牙（25 收官后由 Cursor 通知切换）。

---

## 0 · 目标与非目标

| 要 | 不要 |
|---|---|
| 用户在 Duo 壳上能用 **`ping 192.168.31.1`** / **`net dhcp`**（LwIP 路径），与 Pi 课轨同形 | 只改提示文案、仍强制 `wififw ping` |
| join+装钥后 **武装** AIC：lwIP `linkoutput`→`AicWifiDataSend`；RX→`LwIpNetifInput` | 未装钥就武装；假 `link up` |
| 未武装时 **dwmac 有线** 行为不变 | 半截抢主网卡、有线永死 |
| 新文件 ≤300、命名写全 | 往 `WifiDhcp.c`（已 300）再堆；禁缩写顶全称 |
| 可保留 `wififw dhcp\|ping` 作诊断 | 为过通用 dhcp 再扫 GTK/KeyID 矩阵 |

**柱总判据（netif-5）**：

1. `wififw join` + KEY 成功后武装（自动或 `wififw net attach`）。  
2. 壳 `net dhcp`（或等价 Kick）→ 串口 **`net-dhcp-ok`** 且 `dhcp_ip≠0`。  
3. 壳 **`ping 192.168.31.1`** → 成功（建议落盘/打 **`ping_ok=1`**）。  
4. 未 join 时：有线 `ping`/dhcp 不被 WiFi 代码弄坏。

---

## 1 · 为何现在不通（复现）

```text
Blocks> wifi info
unknown: wifi  (help)          ← HalWifi 未接 Duo
Blocks> ping 192.168.31.1
Net: not available (no link / wifi)   ← LwIP/HalNet 未见武装 WiFi；或无租约/无 link
```

已绿的私有路径（**对照用，不是本柱终点**）：

```text
wififw write confirm → bringup → scan → join Apple <密码> → dhcp → ping
→ WiFi: dhcp: OK ip=… / WiFi: ping_ok=1
```

课网：SSID `Apple`，网关常 `192.168.31.1`；DHCP 须 BOOTP **flags=0**（见 Docs/22）。

---

## 2 · 架构：Pi 已做 vs Duo 缺口

```text
                    ┌──────────── 壳 ────────────┐
                    │ ping / net dhcp / wifi info │
                    └─────────────┬──────────────┘
                                  │
                          HalNet / LwIp*
                                  │
              ┌───────────────────┼───────────────────┐
              │                   │                   │
         dwmac 有线            （缺口）            Pi WiFi
     BoardEthernetSend     武装后应走 AIC      TryLinkOutput
     LwIpNetifInput←ETH    AicWifiDataSend     Feed→LwIpNetifInput
```

| 层 | Pi（已绿） | Duo（现状） | 本柱要补 |
|---|---|---|---|
| TX | `HAL/Arm64/LwIp/Netif.c` `PlatformSendEthernet` 先 `BoardWifiTryLinkOutput` | `HAL/RiscV/LwIp/Netif.c` **只** `BoardEthernetSendFrame` | 同形：先 Try AIC，失败再 dwmac |
| RX 喂 | `HalNetFeedAlternateReceive`→`BoardWifiPollFeedLwIp` | `HalDevicesNet.c` **空函数** | 武装后短喂 → `LwIpNetifInput` |
| 武装 | `BoardWifiArmNetifLinkOutput(mac)` | 无 | 新建 `WifiNetif*.c` |
| L2 发送原语 | `WifiSdpcmSendEthernet` | **`AicWifiDataSend(Host, Frame, Len, Vif, Ap)`** ✅ | 桥接调用，勿重写固件 TX |
| L2 收包 | SDPCM DATA | SDIO + `WifiEapol` 分流；dhcp/arp/icmp 专用 sink | 武装后旁路进 lwIP；**EAPOL 握手期勿拆** |
| DHCP | `LwIpDhcpRestart` | 自研 `wififw dhcp` | 通用路径走 lwIP |
| ping | 壳 `ping`→`LwIpPing` | `wififw ping` | 壳 `ping` 通 |

---

## 3 · 必读文件清单（netif-0 精读）

### 3.1 Pi 对表（只读，禁止改）

| 文件 | 看什么 |
|---|---|
| `Kernel/HAL/Arm64/Board/rpi-3b/Wifi/WifiNetLinkOutput.c` | Arm / Try / KickDhcp；`lwip_out` 计数 |
| `Kernel/HAL/Arm64/Board/rpi-3b/Wifi/WifiNetLwIpFeed.c` | DATA→`LwIpNetifInput`；Feed 上限 |
| `Kernel/HAL/Arm64/Board/rpi-3b/Wifi/WifiNetProbe.c` | 武装时机、Kick 顺序、否证注释 |
| `Kernel/HAL/Arm64/Board/rpi-3b/Wifi/WifiNetDhcpWait.c` | 等租约 |
| `Kernel/HAL/Arm64/Board/rpi-3b/Wifi/WifiNetPingProbe.c` | `LwIpPing` |
| `Kernel/HAL/Arm64/LwIp/Netif.c` | `PlatformSendEthernet` 里 Try 优先 |
| `Kernel/HAL/Arm64/Hal/HalDevicesNet.c` | `HalNetFeedAlternateReceive` |
| `Archive/项目/进展/路线图1031.md` | 7c2…7e 标签 `0x15a`…`0x15e` |

### 3.2 Duo 战场（可改）

| 文件 | 看什么 / 预期改动 |
|---|---|
| `…/Sdhci/WifiEapolTx.c` | **`AicWifiDataSend`** — TX 唯一原语 |
| `…/Sdhci/WifiNetFrame.c` | `AicNetStart/Stop`；收包分给 dhcp/arp/icmp |
| `…/Sdhci/WifiEapol.c` / Parse | 数据帧解密、`gAicEapolKeysInstalled` |
| `…/Sdhci/WifiDhcpLease.c` | 租约（可选复用 MAC/IP 展示） |
| `…/Sdhci/WifiJoin*` / Handshake | 武装挂点候选（KEY 成功后） |
| **新建** `…/Sdhci/WifiNetif*.c`（建议拆：LinkOutput / Feed / Attach） | Arm/Try/Feed；≤300 |
| `Kernel/HAL/RiscV/LwIp/Netif.c` | `PlatformSendEthernet` 加 Try 钩子（对表 Arm64） |
| `Kernel/HAL/RiscV/Hal/HalDevicesNet.c` | 实现 `HalNetFeedAlternateReceive`；武装时 `HalNetGetMac` 可返回站点 MAC |
| `Kernel/Services/Shell/Commands/Wifi.c` | 可选 `wififw net attach`；netif-6 再接 `wifi` |
| `Kernel/Services/Shell/Commands/Net.c` | 一般不改；确认 `ping`→`LwIpPing` 条件 |

### 3.3 有线（默认只读）

| 文件 | 看什么 |
|---|---|
| `…/Ethernet/Protocol.c` | 现有 `LwIpNetifInput` |
| `…/Ethernet/Backend.c` | `DriverNetAttach` |

---

## 4 · 对表表（netif-0 交付：填「结论」列）

| # | 问题 | Pi 锚点 | Duo 锚点 | 结论（netif-0 已填，2026-11-01） |
|---|---|---|---|---|
| 1 | TX 谁把以太网帧送出？ | `WifiNetLinkOutput.c:56` `BoardWifiTryLinkOutput` → SDPCM | `WifiEapolTx.c:196` `AicWifiDataSend(Host,Frame,Len,Vif,Ap)`；**lwIP 出口从未调用它** | Duo 的 TX 原语存在且已被自研 DHCP/ARP 验证可用，**只差"从 lwIP 出口接过来"** |
| 2 | RiscV `PlatformSendEthernet` 现状？ | `HAL/Arm64/LwIp/Netif.c:51` 先 Try WiFi、失败回退 SMSC | `HAL/RiscV/LwIp/Netif.c:46` **只有** `BoardEthernetSendFrame`（dwmac） | netif-1 就在这加"先 Try、失败回退 dwmac"（对表 Arm64 同一处） |
| 3 | RX 谁调 `LwIpNetifInput`？ | `WifiNetLwIpFeed.c:22` → `LwIpNetifInput` | 有线：`Ethernet/Protocol.c:160`；**WiFi 侧零调用**（收帧只进 `WifiEapol`） | netif-2 需新增"非 EAPOL 数据帧 → `LwIpNetifInput`"的分流 |
| 4 | `HalNetFeedAlternateReceive`？ | `HAL/Arm64/Hal/HalDevicesNet.c:183` → `BoardWifiPollFeedLwIp(8u)` | `HAL/RiscV/Hal/HalDevicesNet.c:149` **空函数** | 在此实现每拍短喂（≤8 包，对表 Pi） |
| 5 | 武装 API 名/语义？ | `BoardWifiArmNetifLinkOutput(mac)`（`WifiNetLinkOutput.c:22`），join 后由 `WifiNetProbe.c:241` 调；查询 `BoardWifiLinkOutputIsArmed`（:18） | 无 | **建议同名同语义**：`BoardWifiArmNetifLinkOutput/TryLinkOutput/LinkOutputIsArmed`，实现放 Duo 板级；语义 = join+装钥成功后置位，记录 vif/ap/mac |
| 6 | 站点 MAC 从哪来？ | join/claim 后取站点 MAC | `AicNetStationAddress`（`WifiNetFrame.c:218`：优先 M1 addr1，回退 `AicScanStationAddress`） | 武装时传这个 MAC；`HalNetGetMac` 武装后返回它（lwIP DHCP 依赖） |
| 7 | Vif/Ap 下标？ | 无（SDPCM 自带） | `AicJoinLastIndexes(&vif,&ap)`（`WifiAssocPrivate.h:259`） | Try 里从这里取，取不到就 fail（回 dwmac） |
| 8 | 壳 `ping` 链？ | `Net.c:98 CommandPing` → `LwIpPing` | 同壳：`LWIP_ENABLE` 时走 `LwIpPing`（`Services/Net/LwIp/LwIp.c:114`），否则 `HalNetPing` | **Duo 构建 LWIP=1，已有 lwIP ping**；无 IP 时报 `no reply`。netif-4 后自然通 |
| 9 | 壳 `wifi`？ | `HalWifiSetOps`（`HAL/Arm64/Hal/HalDevices.c:160`） | **RiscV 无 `HalWifiSetOps`**（grep 空） | `unknown: wifi`；netif-6 可选补 |
| 10 | 主网卡冲突？ | SMSC vs WiFi，先 WiFi 后回退 | **dwmac**（`Ethernet/Backend.c` `DriverNetAttach`）vs WiFi | 武装后顺序：Try WiFi → 失败回退 dwmac；未武装不碰 dwmac |
| 11 | lwIP DHCP BOOTP flags？ | 项目封装 `Services/Net/LwIp/Dhcp.c`（`LwIpDhcpRestart`）；lwIP 底层 `Kernel/ThirdParty/lwip/src/core/ipv4/dhcp.c:1945` 显式 `msg_out->flags = 0`（刀 #184 注释） | 自研 `WifiDhcpMessage.c:16` 默认 flags=0（Docs/22 §1a） | **两边默认一致（flags=0）**；若 OFFER 沉默先查 flags，禁回头扫 GTK |
| 12 | EAPOL 与 Feed 如何分期？ | join 完成后 Kick，之后才武装 | 现收帧只进 `WifiEapol`，门控 `gAicEapolKeysInstalled` | Feed 规则：**未装钥不喂**；装钥后按 ethertype 分流（`0x888e` 仍交 EAPOL），避免抢死握手 |

**拟新建文件（netif-1/2/3 用，均 ≤300 行）**

| 文件 | 职责 |
|---|---|
| `Kernel/HAL/RiscV/Board/milk-v-duo-s/Sdhci/WifiNetifLinkOutput.c` | `Arm/IsArmed/TryLinkOutput` + `lwip_out` 计数（≤200） |
| `Kernel/HAL/RiscV/Board/milk-v-duo-s/Sdhci/WifiNetifFeed.c` | `PollFeed` → `LwIpNetifInput` + `lwip_in` 计数（≤200） |
| `Kernel/HAL/RiscV/Board/milk-v-duo-s/Sdhci/WifiNetifAttach.c` | attach/disarm（join 钩子或 `wififw net attach`）（≤150） |
| 改：`HAL/RiscV/LwIp/Netif.c` | `PlatformSendEthernet` 加 Try 钩子（对表 Arm64） |
| 改：`HAL/RiscV/Hal/HalDevicesNet.c` | 实现 `HalNetFeedAlternateReceive`；武装时 `HalNetGetMac` 返回站点 MAC |

**netif-0 判据**：上表填完；可另写「拟改文件列表」；**无行为代码**（或仅 `WiFi: netif: eyes ok`）。

### 4.1 · Cursor PS（2026-11-01）· netif-0

| 项 | 结论 |
|---|---|
| **总评** | ✅ **过** — 十二行结论可跟代码；拟建三文件命名写全、≤300；无行为代码 |
| **抽查** | RiscV `PlatformSendEthernet` 仅 dwmac；Arm64 先 `BoardWifiTryLinkOutput`；RiscV `HalNetFeedAlternateReceive` 空；`AicWifiDataSend` @ `WifiEapolTx.c:196`；Pi Feed=`WifiNetFeedEthernetToLwIp`→`LwIpNetifInput`；`AicNetStationAddress` / `AicJoinLastIndexes` 符号在；`dhcp.c:1945` `flags=0`（路径须写 **`Kernel/ThirdParty/...`**，初稿漏 `Kernel/`） |
| **#8 微调** | 壳在 `LWIP_ENABLE` 下走 `LwIpPing`（对）；但入口先 `HalNetReady()`——当前 Duo 未武装时仍常报 `Net: not available`，netif-3/5 须让 Ready/link 认 WiFi，勿只接 `LwIpPing` |
| **文件名** | `WifiNetifLinkOutput.c` / `WifiNetifFeed.c` / `WifiNetifAttach.c` **准用**（netif-1 起） |
| **下一刀** | ★ → **netif-1**（TX） |

---

## 5 · 刀序详表

> 协作：文首 ★ 是哪刀就只做哪刀。改序须在本节和 1101 同步注明。  
> **当前 ★（2026-11-01 PS）**：netif-3 ✅ 全绿 → 下一刀 **`dwifi-netif-4`**（等 DS TG TS 后 JX）。

### dwifi-netif-0 · 眼睛清单 ✅

| 项 | 内容 |
|---|---|
| **做** | 精读 §3；填 §4 对表表；列出拟新建 `WifiNetif*.c` 文件名（全称、≤300） |
| **判据** | 对表表无空结论；评审能按表找符号 |
| **禁止** | 改 `PlatformSendEthernet` / Feed 行为 |
| **进度** | ✅ PS 2026-11-01（§4.1） |

### dwifi-netif-1 · TX：lwIP → AIC

| 项 | 内容 |
|---|---|
| **做** | ① `WifiNetifLinkOutput.c`：`Arm` / `IsArmed` / `TryLinkOutput` / 计数；Try 内 `AicWifiDataSend`（需 Host+Vif+Ap：从 join 缓存取）。② 改 `HAL/RiscV/LwIp/Netif.c` `PlatformSendEthernet`：`Try==0` 成功；否则 `BoardEthernetSendFrame`。③ 探针：武装后强制发一帧（或临时壳）证明 `lwip_out>0` |
| **判据** | 串口 **`WiFi: net-lwip-out-ok`** 且 **`lwip_out>0`**（计数名可同 Pi） |
| **对表** | Pi `WifiNetLinkOutput.c`；tag 故事 `0x15c` |
| **坑** | 未 join 时 Host/Vif 无效 → Try 须 fail 回 dwmac，**不能**空指针；**Duo 必须定义 `BOARD_HAS_WIFI`**，否则②的 `#if` 整段被剃 |
| **依赖** | netif-0 |
| **进度** | ✅ **收官（2026-11-01 20:12，commit `5d617381`，已 push）**<br>评审全绿后 TG+TS。真机原文：`write confirm→done`、`bringup ok(4/4)`、`scan` 6 APs、`join→eapol handshake ok (ptk+gtk)`、`wifinet attach→netif armed`、`wifinet tx→data tx: len=28 ethertype=0x0806 … mcast=1` + `net-lwip-out-ok lwip_out=1`、`status: armed=1 lwip_out=1 fail=0`；回归 `wififw dhcp→ip=192.168.31.186`。符号：`nm netif5/…/Netif.o → U BoardWifiTryLinkOutput`（netif4 无，对照成立）。三架构 0 error。边界：lwIP 发起帧的运行时证据归 netif-4（DHCP）。 |
| **评审** | ✅ **全绿**（2026-11-01 20:10 Cursor）。抽查：`BoardConfig.h` `#define BOARD_HAS_WIFI 1`；`Netif.c` Try→失败回 dwmac；`nm Build/netif5/.../Netif.o` → **`U BoardWifiTryLinkOutput`**（netif4 无，对照成立）。进度串口：join+handshake → `wifinet attach/tx` → `net-lwip-out-ok lwip_out=1` / `fail=0` 接受为本刀判据。壳 `ping` 仍 `lwip_out=0` **不挡本刀**（归 netif-4，进度④说对了）。命名/≤300 过。小债（不挡 TG）：未用头/宏；Arm 未查装钥→netif-3；`status` hex。**给 DS**：本行已 **全绿** → 允许 **`TG` + `TS`**，1101 ★ 推 **`dwifi-netif-2`**，再 **`JX`**。仍读 TO-DS；未见「切换到 Docs/24」前勿开蓝牙。 |

### dwifi-netif-2 · RX：AIC → LwIpNetifInput

| 项 | 内容 |
|---|---|
| **做** | ① `WifiNetifFeed.c`：从现有 RX 路径取 **已解密以太网帧**（对表 `WifiNetFrame`/`EapolParse` 里 dhcp 看到的 Payload）。② `HalNetFeedAlternateReceive`：武装且 `MaxPackets` 上限（建议 ≤8，对表 Pi）短喂。③ **join/EAPOL 进行中**：不走 lwIP Feed 或 Feed 忽略 0x888e，握手仍用旧路径 |
| **判据** | **`WiFi: net-lwip-in-ok`** 且 **`lwip_in>0`** |
| **对表** | Pi `WifiNetLwIpFeed.c`；`0x15a` |
| **坑** | 喂太多堵壳；喂太少 dhcp 无应答；重复投递同一帧 |
| **依赖** | 默认 netif-1；若证据要先 RX，可对调并改本文+1101 |
| **进度** | 🔧 **红线①②③已改，真机复验完成，待评审复审（2026-11-01 20:50）**<br>**针对评审红线**：<br>① 【sink 常驻】改为 **sink 生命周期 = 武装生命周期**：`Arm`→`BoardWifiFeedAttachSink(1)`，`Disarm`→`(0)` 摘掉；`NetifFrameSink` 内**再查一次** `BoardWifiLinkOutputIsArmed()`（双保险）；`BoardWifiPollFeedLwIp` 不再自己挂/摘 sink。<br>② 【缺 include】`HAL/RiscV/Hal/HalDevicesNet.c` 补 `#include "WifiNetif.h"`；顺带修掉三处隐式声明警告：`WifiEapolEther.c`/`WifiNetifFeed.c` 补 `CString.h`（memcpy/memset），`WifiNetifLinkOutput.c` 补 `WifiHandshakePrivate.h`（AicWifiDataSend）+ `WifiNetPrivate.h`（AicNetStationAddress）。现在 `build.sh riscv duo` **0 warning 0 error**。<br>③ 【进度未交】本格已交，串口原文见下。<br>**真机（Duo USB0）**：`fw state=done` → `bringup ok(4/4)` → `scan` 8 APs → `join` `handshake ok (ptk+gtk)` → `wifinet attach` → `WiFi: netif armed` → `wififw dhcp` → **`WiFi: net-lwip-in-ok lwip_in=1`** → `wifinet status`：`armed=1 lwip_out=0 fail=0 lwip_in=3 drop=0`。<br>**红线①的反向证明**：`wifinet disarm` → `WiFi: netif disarmed` → 再跑 `wififw dhcp` → `dhcp: OK ip=192.168.31.186`（私有路径照常）但 `wifinet status` 仍是 `armed=0 … lwip_in=3`（**不增长**）⇒ Disarm 后确实不再喂 lwIP。<br>**实现要点**：整帧 sink 在 `WifiEapolEther.c` 拼装（802.11 取 addr1=dst/addr2=src；固件已剥 802.11 则照抄前 14 B）；`0x888e` 不进此口；喂包期间开安静窗口（`SdhciMarkSetQuiet`），判据标签在"首帧真喂入"那一刻临时开窗打印。 |
| **评审** | ✅ **全绿**（2026-11-01 20:40；**20:50 复审仍全绿**）。红线①②③与 Disarm 后 `lwip_in` 不增长已对上；commit **`6b163d22`** 已在 main。**给 DS**：netif-2 已可视为收官；请把进度格改成「✅ 收官 + commit」口径；**JX netif-3**（WIP 已见 `HalNetGetMac` 武装优先 / `HalNetEnsureLwIpActive` / handshake Arm——**先填 netif-3「进度」再评**）。 |

### dwifi-netif-3 · 武装 + MAC + 起 lwIP

| 项 | 内容 |
|---|---|
| **做** | KEY 成功后（或 `wififw net attach`）：`Arm(StationMac)`；`HalNetGetMac` 在武装时返回站点 MAC（对表 Pi SyncMac）；若 `!LwIpActive()` 则 `LwIpInitialize()`。明确 **何时 Disarm**（disconnect / 新 join 前） |
| **判据** | 武装后 dump/info 见 WiFi MAC；未 join 时有线 MAC/行为不变；串口一行 `WiFi: netif armed` |
| **对表** | Pi Kick 前 `LwIpInitialize` + Arm |
| **依赖** | netif-1 + netif-2 |
| **进度** | ✅ **收官（2026-11-01 21:42，commit `061fbc3f`，已 push）**<br>评审全绿后 TG+TS。判据原文：join #1 → `WiFi: eapol handshake ok (keys installed: ptk+gtk)` → **`WiFi: netif armed`**（自动，无需 attach）；`wifinet status` → `armed=1 … mac=88:00:33:77:10:99`；武装后 `wififw dhcp` → `net-lwip-in-ok lwip_in=1`；join #2 → **`WiFi: netif disarmed`** 先出现；解除后 `net` → `mac 02:00:00:00:00:01 / link up 100/FD / stack lwip (RX unified)`（有线行为不变）。三架构 0 warning 0 error。<br>**负测（顺带发现，留给 netif-4）**：未武装时 `net dhcp` 会 hold 死壳（只到 `lwip dhcp: queued`），需 reboot。 |
| **评审** | ✅ **全绿**（2026-11-01 21:40 Cursor）。握手后自动 Arm + `HalNetEnsureLwIpActive`；join 入口 Disarm；`HalNetGetMacAddress` 武装优先站点 MAC；`wifinet status` 见 `mac=`；串口 `netif armed` / 再 join `disarmed` / 未武装回有线 MAC——判据齐。命名/`WifiNetif.c`≤300 过。小债（不挡）：`HalNetEnsureLwIpActive` 仅 RiscV 实现（他架构无引用即可）；`HalNetReady` 未改（netif-4/5 若壳卡 Ready 再钉）。**给 DS**：本行全绿 → **`TG`+`TS`** → ★ **`dwifi-netif-4`** → **`JX`**（通用 dhcp；flags=0；勿扫 GTK）。 |

### dwifi-netif-4 · 通用 DHCP（lwIP）

| 项 | 内容 |
|---|---|
| **做** | 武装后：`LwIpDhcpRestart(…)` 或壳 `net dhcp`；循环 `LwIpService` + `HalNetFeedAlternateReceive`；等 `netif` 有 IP；打 **`net-dhcp-ok`** / `dhcp_ip=a.b.c.d` |
| **判据** | **不依赖** `wififw dhcp` 拿到课网 IP |
| **对表** | Pi `WifiNetDhcpWait.c`；`0x15d` |
| **坑** | 若无 OFFER：**先查 lwIP Discover 的 BOOTP flags**（Docs/22：课网要 0）。禁止回头扫 GTK |
| **依赖** | netif-3 |
| **进度** | ⬜（等 netif-3 TG 后 JX） |
| **评审** | ⬜ |

### dwifi-netif-5 · 通用 ping 验收

| 项 | 内容 |
|---|---|
| **做** | 壳 `ping 192.168.31.1`（`Net.c`→`LwIpPing`）；成功打 **`ping_ok=1`**（可落盘对表 Pi）。回归：`wififw ping` 仍可可选保留 |
| **判据** | 真机壳 ping 通；1101 netif-5 进度可贴串口原文 |
| **对表** | Pi `0x15e` |
| **依赖** | netif-4 |

### dwifi-netif-6 ·（可选）`wifi` 门面

| 项 | 内容 |
|---|---|
| **做** | `HalWifiSetOps` 薄封装 scan/join/info，避免 `unknown: wifi`。Duo **不做托盘** |
| **判据** | `wifi info` 有 MAC/IP/SSID 一类字段 |
| **进度** | ⏸ 不挡 netif-5 收官 |

---

## 6 · 建议新建文件（命名示例，可微调）

| 文件 | 职责 | 行数预算 |
|---|---|---|
| `Sdhci/WifiNetifLinkOutput.c` | Arm / Try / 计数 | ≤200 |
| `Sdhci/WifiNetifFeed.c` | PollFeed → LwIpNetifInput | ≤200 |
| `Sdhci/WifiNetifAttach.c` | attach/disarm 壳或 join 钩子 | ≤150 |
| 板级头或 `WifiNetPrivate.h` 增补 | 原型；禁砍字 | — |

公开符号建议与 Pi 同形便于评审：`BoardWifiArmNetifLinkOutput` / `BoardWifiTryLinkOutput` / `BoardWifiPollFeedLwIp`（实现放 Duo Board；或 `AicWifi*` 前缀但语义注释写明对表 Pi）。

---

## 7 · 真机手测脚本（netif-4/5）

```text
# 部署：deploy.sh --port /dev/ttyUSB0 ；reboot；确认 build= 新
wififw write confirm
wififw bringup
wififw scan
wififw join Apple <密码>
# 若未自动武装：
wififw net attach          # netif-3 若提供
net dhcp                   # 或文档写明的 Kick
ping 192.168.31.1
```

串口应能 grep：`net-lwip-out-ok` / `net-lwip-in-ok` / `net-dhcp-ok` / `ping_ok`（或壳 `ping` 成功行）。

NUC monitor **非必须**（本柱是主机栈）；若查空口 OFFER，≤12s 后立刻回 managed。

---

## 8 · 禁止与评审红线

### 禁止

- 改 `HAL/Arm64/Board/rpi-3b/Wifi/**`  
- 开 Docs/24 蓝牙刀 / 改 BT stub「顺便」  
- 为 dhcp 再扫 `MM_KEY_ADD` / KeyID / noptk  
- 对 Duo 跑 `ck-pi-sd`  
- 未口令 commit/push  
- 单文件 >300；`Cfg`/`Init`/`Tx`/`Rx` 等禁缩写顶全称  

### Cursor 评审勾选

- [ ] `PlatformSendEthernet` 未武装时仍只走 dwmac？  
- [ ] Try 失败是否回落有线（非直接 ERR）？  
- [ ] Feed 是否限制每拍包数？EAPOL 期是否未抢握手？  
- [ ] 武装后 `HalNetGetMac` 是否站点 MAC（lwIP dhcp 需要）？  
- [ ] 无 OFFER 时是否查了 **flags**（Docs/22）？  
- [ ] 判据串口原文是否写入 1101 对应刀进度？  
- [ ] 命名 / 行数自查是否过？  

---

## 9 · 开场口令

```text
# netif-3 已 ✅ 全绿 → 先：
TG
TS
# 再：
JX
只做 1101 ★ = dwifi-netif-4
权威：Docs/25 netif-4
LwIpDhcpRestart 或 net dhcp；BOOTP flags=0
禁扫 GTK；填进度等评审，非全绿不 TG
```
