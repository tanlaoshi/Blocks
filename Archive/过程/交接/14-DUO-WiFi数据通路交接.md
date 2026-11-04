# 14 · DUO WiFi 数据通路 —— 交接给 Cursor

> 写于 2026-11-01 04:15。**关联 + WPA2 握手 + 装密钥已真机打通**，卡在数据通路。
> 本文只写"已证明的"和"已排除的"，避免接手的人重走。

---

## 1 · 已经真机验证通过的（不用怀疑）

```
SDIO 枚举 → 固件下载(5 段+101 patch) → ME_CONFIG/CHAN → MM_START
→ 扫描（多轮，真实 SSID） → 关联（SM_CONNECT_IND status=0）
→ 四次握手 M1/M2/M3/M4（M3 的 MIC 我们用 PTK 验过 = AP 与我们同 PTK）
→ 装密钥 PTK(CCMP) + GTK(TKIP)，固件回 cfm=0
```

**关键判据**：M4 发出去之后 **AP 不再重传 M3**（M3 replay 停在 1）⇒ AP 接受了 M4。
（修复前是"装钥匙→发 M4"，M4 被固件加密，AP 收不到，M3 replay 涨到 3。这是真 bug，已修。）

**我们发出的 M4 与 Linux 的 M4 前 49 字节完全一致**（`01 03 00 5f | 02 | 03 0a | 00 00 | replay | nonce=0`）。

---

## 2 · 卡在哪（一句话）

**DHCP 的广播帧，固件用 PTK/单播密钥加密（空口 802.11 头 KeyID=0），AP 直接丢。**
正确的应该是 GTK、KeyID=2（抓包对照：Linux 的 DHCP 广播就是 `KeyID 2`）。

## 3 · 已排除（都刷过卡、都以真机结果为准）

| 尝试 | 结果 |
|---|---|
| GTK keyid = 0/1/2、cipher = TKIP/CCMP、pairwise = 0/1、长度 16/32 | 全无 OFFER |
| 广播 staid = ap_idx(0) | 帧能上air但 KeyID=0（PTK 加密）→ AP 丢 |
| 广播 staid = 0xFF / 32 / 8 / 10 / 16 / 64 / 128 | **固件根本不发**（抓包 262 帧、我们 0 帧） |
| 广播 ac = 0/1/3、tid = 0/255 | 无变化 |
| M4 的 install 位 0/1 | 无变化 |
| 装完钥匙再发一次 `ME_SET_CONTROL_PORT` | 无变化 |
| 单播 ARP 问 AP 自己（DA=BSSID，192.168.31.1） | **AP 不回** |

**`data after keys: decrypted=0` 从没变过** ⇒ 固件既没用 GTK 加密发送，也从未解密过任何收到的帧。

## 4 · 最值得接着查的三条（按可能性）

1. **厂商 BCMC 队列没有真的建起来**
   厂商 `rwnx_tx.c:511-521`：广播走 `NX_BCMC_TXQ_NDEV_IDX` 队列，
   `sta_idx = NX_REMOTE_STA_MAX(32) + master_vif_idx(vif)`。
   我们填 32 时固件直接丢帧 ⇒ 说明固件里没有这个 BCMC 站。
   **查 `MM_ADD_IF` 之后固件是否创建 BCMC sta**（可能要先 `MM_ADD_IF` 带某个 flag，或先建一条 sta 表项）。

2. **GTK 装到了一个固件不用于 TX 的槽位**
   我们传的 44 字节已逐字段核对（dump 在 `WifiKeyAdd.c`）：
   `key_idx=2 sta=0xFF len=32 cipher=TKIP inst=0 spp=0 pairwise=0`，`cfm=0`。
   **但从未验证过固件是否真的写进了它的 key table** —— 没有"读回密钥"的消息。

3. **收方向：固件根本没解密**
   `decrypted=0` 且 AP 无任何回帧。若单播都能通（我们发出去 AP 该回 ARP），
   说明问题在**发方向的密钥选择**，不在收方向。

## 5 · 现成的工具（直接用，别重做）

- **文件探针**：板子读 `BLOCKS:probe.cfg`，一行一个键值，`wififw dhcp` 时生效：
  ```
  mcast=<n>   广播帧 txdesc staid 覆盖
  ac=<n>      txdesc ac 覆盖
  tid=<n>     txdesc tid 覆盖
  start=1     先发 EAPOL-Start 探针
  arpuni=1    单播 ARP 问 AP（192.168.31.1）
  m4install=0 M4 不置 install 位（join 时读）
  ```
  改文件用 `python3 Tools/ksend.py --port /dev/ttyUSB0 --file probe.cfg --path BLOCKS:probe.cfg --no-reboot --no-backup`（2 秒）。
- **部署**：`Kernel/HAL/RiscV/Board/milk-v-duo-s/scripts/deploy.sh --no-build --elf <elf> --port /dev/ttyUSB0`（约 3 分钟，走串口差量）。
- **抓包**：NUC(192.168.31.47, tank/1989) 上 `iw dev wlp1s0 set monitor control; iw dev wlp1s0 set channel 8; tcpdump -i wlp1s0 -e -w /dev/shm/x.pcap`。
  ⚠️ **这台 NUC 在 monitor 模式下会过热掉线**（跑超过 ~30 秒必挂），要短抓、抓完立刻 kill。

## 6 · 代码位置

```
Kernel/HAL/RiscV/Board/milk-v-duo-s/Sdhci/
  WifiEapolTx.c        AicWifiDataSend（txdesc 组装）+ 探针覆盖点
  WifiKeyAdd.c         MM_KEY_ADD_REQ 镜像 + 44 字节 dump
  WifiHandshakeInstall.c  GTK 解包 → 装 PTK/GTK → 发 M4 → 盯解密帧
  WifiHandshakeKey.c   M2/M4 组帧、MIC
  WifiDhcp.c           DHCP 客户端 + probe.cfg 解析 + 两个探针
  WifiArp.c            ARP（含新加的 AicArpResolveUnicast）
```

## 7 · 一句话给接手的人

**别再从"关联/握手"找问题** —— 那一段已经用 M4 被接受证明了。
**只查一件事：广播帧为什么不是用 GTK 加密发出去的。**
厂商驱动是靠"BCMC 队列 + sta_idx=32+vif"做到的，而固件在我们这里拒收这个 sta_idx ——
**从这里往下查，或者干脆去问固件/拿到厂商对 D80 的 BCMC 建站流程。**
