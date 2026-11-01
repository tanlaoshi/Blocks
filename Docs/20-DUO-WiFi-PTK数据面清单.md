# 20 · DUO WiFi PTK 数据面（dwifi-data-ptk）

> data-1：`unicast-data-fail`。一次一变量。未通 → **不 TG**。

---

## 0 · 否证表（本刀累计）

| 变量 | 真机 | 结论 |
|---|---|---|
| gtkmode=0 | 仍 fail | 盖槽≠唯一根因 |
| portearly=0（默认） | M1 仍到；仍 fail | 早开≠根因；**默认保留**（对表厂商） |
| nogtk=1 | 仍 fail | GTK 装钥≠挡单播唯一根因 |
| start=1 | 装钥后 EAPOL-Start 无应答 | 单播 EAPOL AP 不理 |
| tid=255 | 仍 fail | tid≠根因 |
| hostid=1 | 仍 fail | SDIO hostid 形≠根因 |
| noauthz=1 | `AUTHORIZED skipped`；仍 fail | AUTHORIZED≠弄坏唯一根因 |
| M4_BEFORE→0 | **不做** | Docs/14·15 已否：先装后发 M4 密文 AP 收不到 |
| nocalib（停发 0x69） | 仍 fail | RF_CALIB 软失败≠单播根因；默认继续不发 |
| ADD_IF MAC=厂商（efuse / dflt） | 仍 fail | 本机 MAC≠单播根因；efuse 全 0→`88:00:33:77:10:99` |
| 空口 PN=重传（§1g） | IV:1×3 带 Retry | **否证 PN 重用** |
| 离线 CCMP（§1g） | 同会话 TK 解 ARP MIC OK | **否证错 TK / TX MIC** |
| icmpuni=1（§1h） | `unicast-icmp-fail` | **否证「仅 ARP 特例」**；AP 对单播 L3 亦沉默 |
| uapsd=0（§1i） | 仍 `unicast-data-fail` | **否证 UAPSD 广告**；默认保留 0（无 trigger） |

观察：从未 `decr≠0`；PTK `hw_key_idx=16` GTK `=2` cfm=0；组播空口曾 KeyID=0（Docs/14）。  
**收口判断**：单播 TX 密文已证对（§1g），AP 对 ARP/ICMP 均不回（§1h/§1i）。本课网下 `unicast-data-ok` **无法再靠 PTK/控制口刀刷绿**——须组播 GTK（data-2）拿 DHCP，再验单播。

---

## 1 · MM_SET_RF_CALIB（已试）

厂商末步 `MM_SET_RF_CALIB`。真机：`rf calibrate FAILED: no CFM(0x6a)`。  
→ **否证「只补 calib 就能通」**；厂商路径前有 `txpwr_lvl_v3/adj/ofst2x`。bringup 对 calib **软失败**（me/mm 已 ok）。

## 1b · txpwr_lvl_v3 → RF_CALIB（本刀 · 半绿）

对表 `aicwf_set_rf_config_8800d80`：`WifiTransmitPowerLevel.c`（Duo userconfig 表）→ `WifiRfCalibrate.c`。  
adj/ofst2x 在 Duo userconfig 为 enable=0（厂商 skip，未发）。

| 步 | 真机 |
|---|---|
| `MM_SET_TXPWR_IDX_LVL` 0x77 | ✅ `transmit power level ok (cfm len=0)` id=0x78 |
| `MM_SET_RF_CALIB` 0x69 | ❌ 仍 `no CFM(0x6a)`（tx 已出；fn1 无 pending） |
| join / AUTHORIZED / key cfm | ✅ 同前 |
| `arpuni=1` | ❌ `unicast-data-fail`；`decrypted=0` |

→ **否证「MM_START 后再补 txpwr_lvl_v3 就能让 RF_CALIB CFM / 单播通」**。txpwr 可保留。

## 1c · RF 挪到 ME_CONFIG 前（否证）

厂商：`rwnx_ic_rf_init` 在 probe 里早于 `MM_RESET`/`ME_CONFIG`。  
本仓试：txpwr CFM 仍绿；RF_CALIB 仍无 CFM；**随后 ME_CONFIG 无 CFM → bringup 死**。  
→ **否证「RF 后不 RESET 就 ME」**。

## 1d · RF → MM_RESET → ME（否证）

相对 §1c 只加 `MM_RESET`。真机：txpwr ok；RF_CALIB 仍无 CFM；**随后 `MM_RESET` 亦无 CFM → bringup 死**。  
→ **否证「缺的只是 RESET」**；无 CFM 的 RF_CALIB 会卡住整段 MM。已回退 me/mm 后软发 RF。

## 1e · 停发 RF_CALIB，只留 txpwr（本刀 · 否证）

**单变量**：`WifiBringup` 默认**不发** `MM_SET_RF_CALIB`（仍发 `txpwr_lvl_v3`）。  
假说：无 CFM 的 0x69 即使软失败也可能弄脏 MM/数据面。

| 步 | 真机 |
|---|---|
| bringup | ✅ `transmit power level ok` + `rf calibrate NOT sent` |
| join / key cfm | ✅ PTK/GTK `cfm=0`；AUTHORIZED；handshake ok |
| `arpuni=1` | ❌ `unicast-data-fail`；DHCP 无 OFFER |

→ **否证「停发 RF_CALIB 就能通单播」**。txpwr 可留；0x69 默认继续不发（无害且避免卡 MM）。  

## 1f · ADD_IF 用厂商 STA MAC（本刀 · 否证）

**单变量**：对表 `rwnx_main.c`——`MM_GET_MAC_ADDR`（`get=1`）取 efuse；全 0 则用厂商 `dflt_mac` `88:00:33:77:10:99`，不再硬编码 `02:00:00:00:00:01`。  
`WifiStationAddress.c`；bringup 末尾加载，供后续 `MM_ADD_IF`。

| 步 | 真机 |
|---|---|
| bringup | ✅ `efuse all-zero → vendor default 88:00:33:77:10:99` |
| join / key cfm | ✅ PTK/GTK `cfm=0`；AUTHORIZED；handshake ok；`station=88:00:33:77:10:99` |
| `arpuni=1` | ❌ `unicast-data-fail`；`data after keys: decrypted=0` |

→ **否证「本机 ADD_IF MAC（02 vs 厂商）是单播根因」**。厂商 MAC 路径保留（对齐 Linux）。  
下一刀候选：空口抓单播 KeyID/是否上空气（NUC/本机 monitor）；或 TX 描述符里尚未单独证伪的字段（`flags` / `packet_len` 契约再对表）。

## 1g · 空口对齐（NUC monitor ch8）· PN / TX-MIC

抓包：`/tmp/duo-air2.pcap`（初抓）+ `/tmp/duo-air3.pcap`（与串口同会话，`tk=1eaa618f9eb1775151e618a71113874a`）。  
STA=`88:00:33:77:10:99` BSSID=`1c:36:bb:04:fd:80`。

| 观察 | 证据 | 结论 |
|---|---|---|
| 单播 ARP 上天 | `Data IV:1 KeyID 0`，DA=BSSID，AP ACK | TX 路径通；空口 KeyID=0（PTK）正确 |
| IV:1×3 | 802.11 **Retry** 同 PN，非主机重复提交 | **否证「PN 不递增 / 重用」**；勿改 PN/TX 描述符「修 PN」 |
| 无 AP→STA 数据 | 握手后无加密帧 DA=本 STA（有 Assoc/EAPOL/ADDBA） | AP 未回 ARP（不是「我们 RX 解不开所以看不见」） |
| 组播 | QoS `KeyID 0`（CCMP PN）+ 非 QoS `KeyID 1/2`（TKIP IV） | GTK 偶发选中；组播仍常先走 PTK → data-2 |
| 离线 CCMP | 同会话 TK 解 PN=1：MIC OK；明文 ARP `who-has 192.168.31.1 tell 192.168.31.250` | **否证「装错 TK / TX MIC 失败」** |

→ 单播卡点从「密钥/选钥/PN」迁到：**AP 已能解我们的单播，却不回**（策略 / 端口 / 其它）。  
下一刀：`icmpuni=1`（以太网 DA=BSSID 的 ICMP Echo，绕过 ARP）——有回则 ARP 特例；仍无回则 AP 对单播 L3 一律沉默。

## 1h · icmpuni（本刀 · 否证）

**单变量**：`BLOCKS:probe.cfg` 写 `icmpuni=1`（关 `arpuni`）；`WifiIcmpEcho.c`。  
假说：AP 对合法加密 ARP 不回，但对「DA=BSSID 的 ICMP Echo」会回。

| 步 | 真机 |
|---|---|
| join / key cfm | ✅ handshake ok；PTK/GTK `cfm=0` |
| `icmpuni=1` | ❌ `unicast-icmp-fail`（两发 Echo，无 reply；`decr=0`） |

→ **否证「只是 ARP 特例」**：AP 对单播 L3（ICMP）同样沉默。与 §1g「TX MIC OK + 空口无 AP→STA」一致。

## 1i · uapsd_queues=0（本刀 · 否证）

**单变量**：`AIC_JOIN_UAPSD_QUEUES` 厂商默认 VO=`1` → **`0`**（不声明 U-APSD）。  
假说：无 PS-Poll/trigger 却广告 UAPSD → AP 缓单播下行。

| 步 | 真机 |
|---|---|
| join / key cfm | ✅ handshake ok |
| `arpuni=1` | ❌ 仍 `unicast-data-fail` |

→ **否证**。默认保留 `0`。  
**ptk 刀收口**：否证表已覆盖装钥/控制口/MAC/PN/TX-MIC/ARP/ICMP/UAPSD；绿判据 `unicast-data-ok` 在本 AP 上被「须先 DHCP」挡住 → **TG 后进 data-2**。

---

## 2 · 已落地（可保留）

- 默认 `portearly=0`；装钥后 AUTHORIZED  
- `probe.cfg`：`gtkmode` / `portearly` / `nogtk` / `noauthz` / `hostid` / `icmpuni`；缺键清零  
- `WifiHandshakeRun.c` / `WifiHandshakeMessage2.c` 拆分（≤300）  
- `WifiRfCalibrate.c` / `WifiTransmitPowerLevel.c`  
- `WifiStationAddress.c`：efuse / 厂商 dflt_mac → ADD_IF  
- `WifiIcmpEcho.c`：`icmpuni=1` 探针
