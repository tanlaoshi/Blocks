# 25 · DUO WiFi 挂通用网 — DeepSeek 执行包

> **权威刀序 / ★**：[`Archive/路线图1101.md`](../Archive/路线图1101.md)#dwifi-netif（以日档为准）。  
> **本文**：对表路径、禁止项、评审红线；每刀细节以 1101 表为准。  
> **模式**：DeepSeek **JX 执行** → Cursor **评审** → 用户 **TG/TS**。

---

## 0 · 一句话

Pi 已在 1031 把 WiFi 挂进 LwIP（`ping`/`net dhcp` 通用）。Duo 仅 `wififw dhcp|ping` 绿。本柱把 AIC **武装进** `HalNet`/`LwIpNetif`，课轨口令与 Pi 同形。

---

## 1 · 对表表（netif-0 必须填完）

| # | 问题 | Pi 锚点 | Duo 锚点 | 结论（netif-0 已填） |
|---|---|---|---|---|
| 1 | TX 谁把以太网帧送上网卡？ | `HAL/Arm64/LwIp/Netif.c:51` `PlatformSendEthernet` → 先 `BoardWifiTryLinkOutput`（`Wifi/WifiNetLinkOutput.c:56`），失败回退 SMSC | `HAL/RiscV/LwIp/Netif.c:46` `PlatformSendEthernet` → 只有 `BoardEthernetSendFrame`（dwmac）；WiFi 侧仅有 `AicWifiDataSend`（`Sdhci/WifiEapolTx.c:196`），**lwIP 出口完全没有 WiFi 分支** | **Duo 要仿 Pi：在 `PlatformSendEthernet` 里加"武装则走 WiFi、否则回退 dwmac"** |
| 2 | RX 谁调用 `LwIpNetifInput`？ | `Wifi/WifiNetLwIpFeed.c:22` `WifiNetFeedEthernetToLwIp` → `LwIpNetifInput`；由 `Hal/Arm64/Hal/HalDevicesNet.c:183` 每拍 `BoardWifiPollFeedLwIp(8)` 驱动 | 有线：`Ethernet/Protocol.c:160` 调 `LwIpNetifInput`；**WiFi 侧没有任何调用**（WiFi 收帧只进 `WifiEapol` 解析） | **Duo 要新增分流：WiFi 收到的非 EAPOL 数据帧 → `LwIpNetifInput`；且不能把 EAPOL 抢死** |
| 3 | 武装开关在哪？ | `Wifi/WifiNetLinkOutput.c:22` `BoardWifiArmNetifLinkOutput`，join 成功后由 `Wifi/WifiNetProbe.c:241` 调用；查询 `BoardWifiLinkOutputIsArmed`（:18） | **无** | **Duo 需要等价开关**（join+装钥成功后置位），TX 与 Feed 两处都看它 |
| 4 | `HalNetFeedAlternateReceive` | `HAL/Arm64/Hal/HalDevicesNet.c:183` → `BoardWifiPollFeedLwIp(8u)`（仅 `BOARD_HAS_WIFI`） | `HAL/RiscV/Hal/HalDevicesNet.c:149` **空函数** | **Duo 在这里每拍喂 WiFi 收包**；注意与 `wififw` 的 EAPOL 轮询共用一条 poll 路径，否则互相抢 |
| 5 | 壳 `ping` 调用链 | 壳 `Services/Shell/Commands/Net.c` → `HalNetPing`（`HAL/Arm64/Hal/HalDevicesNet.c:103`）→ `DriverNetPing`（`Core/Driver/Net.c:70`）→ 后端 `Ping`（lwIP） | 同壳 → `HAL/RiscV/Hal/HalDevicesNet.c:91` `HalNetPing` → `DriverNetPing`；后端目前是 dwmac/自研 | **Duo 武装后 ping 要走 lwIP 的 `LwIpPing`**；前提是 netif 的 linkoutput 已接 WiFi（依赖 #1） |
| 6 | 壳 `wifi` | `HAL/Arm64/Hal/HalDevices.c:160` `HalWifiSetOps`，板级 `BoardWifiRegister` 调（`Wifi/Wifi.c:292`） | **RiscV 没有 `HalWifiSetOps` 实现**（grep 为空）→ `wifi` 走 `unknown` | netif-6 可选：给 Duo 补一个 `HalWifiSetOps`（只影响 `wifi` 壳命令，不挡 netif-5 收官） |
| 7 | 主网卡冲突 | SMSC（USB 有线）vs WiFi：`PlatformSendEthernet` 先试 WiFi、失败回退 SMSC | dwmac（`BoardEthernetSendFrame`）vs WiFi（待接） | **Duo 采用同样顺序**：未武装/发送失败必须回退 dwmac，不得抢 |
| 8 | DHCP flags | lwIP `ThirdParty/lwip/src/core/ipv4/dhcp.c:1945` `msg_out->flags = 0`（单播应答）；`lwipopts.h:38` `LWIP_DHCP=1` | 自研 `Sdhci/WifiDhcpMessage.c:16` 默认 **flags=0**，`probe bcast=1` 才置 `0x8000`（Docs/22 §1a） | **两边默认一致（flags=0）**；接 lwIP 后若又出现"广播 OFFER 沉默"，先查 flags 再怀疑密钥 |

**netif-0 结论（三句话）**

1. **Duo 的 WiFi 与 lwIP 之间现在是通的"零"** —— TX（`PlatformSendEthernet`）和 RX（`LwIpNetifInput`）都没有 WiFi 分支；`AicWifiDataSend` 只被自研 DHCP/ARP 用。
2. **Pi 的形态可以直接照搬**：`PlatformSendEthernet` 先 WiFi 后 SMSC + `HalNetFeedAlternateReceive` 每拍喂 + `BoardWifiArmNetifLinkOutput` 武装标志。
3. **Duo 特有的风险是"抢 EAPOL"**：WiFi 收包当前只喂 `WifiEapol`；接 lwIP 时必须按类型分流（EAPOL 0x888e 归握手，其余归 netif），否则会把 `join` 弄挂。

---

## 2 · 刀序速查

| 刀 | 判据标签（建议串口原文） |
|---|---|
| **netif-0** | ✅ 已完成（2026-11-01）：对表表 8 项全部填了锚点与结论；未改行为代码 |
| **netif-1** | `net-lwip-out-ok` `lwip_out>0` |
| **netif-2** | `net-lwip-in-ok` `lwip_in>0` |
| **netif-3** | 武装后 MAC/link 正确；未 join 不抢 dwmac |
| **netif-4** | `net-dhcp-ok` `dhcp_ip≠0`（lwIP 路径） |
| **netif-5** | 壳 `ping …` → `ping_ok=1` |
| **netif-6** | 可选 `wifi info`；不挡 5 收官 |

---

## 3 · 禁止

- 改 `HAL/Arm64/Board/rpi-3b/Wifi/**`「对齐」  
- 改 Docs/24 蓝牙树 / 开 BT 刀  
- 为过 dhcp 再扫 GTK `MM_KEY_ADD` 矩阵  
- `Ready=1` / link up 而无真 TX/RX  
- 单文件 >300、禁缩写顶全称  
- 对 Duo 跑 `ck-pi-sd`  
- 未口令 commit/push  

---

## 4 · 评审红线（Cursor）

- [ ] TX/RX 是否都进了 LwIP（不是只改壳文案）？  
- [ ] 未武装时 `ping`/有线是否仍走 dwmac？  
- [ ] join/EAPOL 是否仍可用（未被 Feed 抢死）？  
- [ ] lwIP DHCP 无 OFFER 时是否先查 **BOOTP flags**（Docs/22）而非猜密钥？  
- [ ] 命名 / ≤300 / 一次一刀？  
- [ ] 真机串口原文是否可贴进 1101 进度格？  

---

## 5 · 开场口令

```text
JX
只做 1101 ★ dwifi-netif-0（Docs/25 对表表）
勿改 Pi；勿开蓝牙；不 commit
```
