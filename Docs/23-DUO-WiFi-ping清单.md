# 23 · DUO WiFi ping（dwifi-ping）

> 承接 Docs/22：dhcp 绿（`flags=0`）。对表 Pi wifi-7e：壳级 ping 网关。  
> 判据：串口 `ping_ok` / `ping_ok=1`。

---

## 0 · 做法

| 项 | 内容 |
|---|---|
| 命令 | `wififw ping [ms]`（默认 4000） |
| 前置 | 同开机已 `wififw dhcp` 成功（租约含 router） |
| 路径 | ARP 网关 → ICMP Echo（DA=网关 MAC）→ `ping_ok` |
| 战场 | `WifiPing.c` / `WifiDhcpLease.c`；复用 `AicIcmpEchoUnicast` |

---

## 1 · 真机（2026-11-01，`build=18:47:30`）

| 步 | 结果 |
|---|---|
| dhcp | ✅ `OK ip=192.168.31.186`（租约入库） |
| ping | ✅ `ping: target=192.168.31.1` → `icmp: reply ok` → **`ping_ok=1`** |

→ **绿**。Duo WiFi 故事与 Pi wifi-7e 同级（壳 dhcp+ping）。可 **TG**；★ 空或转工程债。

---
