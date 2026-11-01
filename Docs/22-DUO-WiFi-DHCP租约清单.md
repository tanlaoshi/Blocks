# 22 · DUO WiFi DHCP 租约（dwifi-dhcp）

> 承接 Docs/21 §1f：ToDS PTK 通、AP FromDS 已转发 Discover；**无 OFFER**。  
> 一次一变量。未通 → **不 TG**。

---

## 0 · 基线（已有 pcap / 明文载荷 · 2026-11-01）

### 空口（NUC monitor，`/tmp/duo-gtkfirst-nuc.pcap` 等）

| 观察 | 证据 |
|---|---|
| STA→AP Discover | ToDS QoS **KeyID=0**，A3=`ff:ff:…`，`orig≈438` |
| AP 转发 | 紧随 FromDS RA=`ff:ff` TA=AP A3=STA **KeyID=gtk**，`orig≈440` |
| **OFFER** | Discover 之后 **无** FromDS `RA=STA`，也 **无** FromDS `RA=ff:ff`∧`A3=ff:ff` 的 DHCP 体量帧 |

→ 不是「OFFER 上了空口但我们 RX `decr=0` 看不见」；**课网在本 STA 的 Discover 之后根本没把 OFFER 送上 WiFi**。  
→ 与 Docs/14「KeyID=0 ⇒ AP 丢 Discover」**已否**（§1f + 本表）。

### 载荷（`ptkdel` 明文 Discover 片段，RFC 2131/2132）

| 字段 | 值 | 结论 |
|---|---|---|
| Eth | `ff:ff` ← STA，IPv4 | 合法 |
| IP | `0.0.0.0→255.255.255.255` totlen=328，头校验 **OK** | 合法 |
| UDP | 68→67 len=308，checksum=**0**（RFC 768 允许；Pi 路径同） | 合法 |
| BOOTP | op=1 htype=1 hlen=6 flags=**0x8000** chaddr=STA cookie=99.130.83.99 | 合法 |
| 选项 | 53=Discover，61=htype+MAC，55=1/3/6，255 | 最小可用集 |

→ **否证「组帧明显违例」** 为无 OFFER 主因（至少在已解码字段上）。

### 卡点（一句话）

AP 已桥接转发 Discover，DHCP 服务端（或上联）**未对本 chaddr 回 OFFER（空口无）**。查：flags/选项差异、池/过滤、对照同 BSS 正常客户端。

---

## 1a · flags=0（清 BOOTP BROADCAST 位）

**单变量**：`flags` 从 `0x8000` → `0`（RFC 2131 §2：不置 BROADCAST 时服务器应向 chaddr **单播** OFFER）。  
假说：课网对「广播应答」路径沉默/丢弃，但对单播 OFFER 会回。

| 探针 | 初测 `nobcast=1`；收口后 **默认 flags=0**，对照用 `bcast=1` |
|---|---|
| 战场 | `WifiDhcpMessage.c` / `WifiDhcpProbe.c` |
| 抓包 | NUC monitor **≤12 s** → 立刻回 managed；snap 建议 ≥400（旧抓 snap=160 只够看 FC/KeyID） |
| 判据 | 串口 `OFFER rx` + `dhcp: OK ip=` → 绿；仍无 → 否证本变量 |

| 步 | 真机（2026-11-01 18:30+，`build=18:26:48`，探针 `nobcast=1`） |
|---|---|
| probe | ✅ `nobcast=1 (BOOTP flags=0)` |
| DISCOVER | ✅ `tx len=342`；首窗无应答后重发一次 |
| OFFER | ✅ `OFFER rx yiaddr=192.168.31.186 server=192.168.31.1 decr=3` |
| ACK | ✅ `ACK rx … mask=255.255.255.0 gw=dns=192.168.31.1` |
| 租约 | ✅ `dhcp: OK ip=192.168.31.186` |
| ARP | ✅ `ARP gw ok`；`data frame after keys … decr=3` |

→ **绿**。已把 **默认 flags=0** 写入组帧；`bcast=1` 可复现旧无 OFFER。  

| 复测 | 真机（`build=18:36:41`，probe 空，无 nobcast） |
|---|---|
| 租约 | ✅ `dhcp: OK`（默认 flags=0 即通） |

→ ★ 本刀可 **TG**；其后 [`dwifi-ping`](../Archive/路线图1101.md#dwifi-ping)。

---
