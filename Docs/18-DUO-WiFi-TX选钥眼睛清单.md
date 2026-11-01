# 18 · DUO WiFi TX 选钥眼睛清单（dwifi-data-0）

> 只读对齐厂商 + 手测 checklist。**不改选钥行为**。  
> 战场：`WifiEapolTx.c` / `WifiKeyAdd.c` / Docs/14。抓包机：NUC monitor ≤30 s。

---

## 1 · 厂商口径（对表 `rwnx_tx.c` / `rwnx_send_key_add`）

| 场景 | hostdesc.staid | 密钥从哪来 |
|---|---|---|
| **STA** 单播 | `vif->sta.ap`（= `SM_CONNECT_IND.ap_idx`） | PTK（pairwise，`sta_idx=ap_idx`） |
| **STA** 组播/广播 | **仍是** `vif->sta.ap`（同一分支） | **GTK 默认钥**（`MM_KEY_ADD` 时 `sta_idx=0xFF`，`pairwise=0`，`key_idx`=KDE KeyID，常为 2） |
| **AP/mesh** 组播 | `sta_table[NX_REMOTE_STA_MAX+vif]`（D80：`32+vif`） | BCMC；**STA 模式不要走这条** |

要点：STA 组播帧的 **staid 填 ap_idx 是对的**；空口 KeyID 应由固件按「DA 组播 → 默认 GTK」选出。若空口仍 KeyID=0，问题在 **GTK 默认钥是否真正参与 TX 选钥**，不是「漏填 32」。

---

## 2 · Blocks 现状（基线，Docs/14 + 读码）

| 项 | 值 |
|---|---|
| 默认组播 staid | `ApIndex`（`probe.cfg` `mcast=` 可覆盖） |
| GTK **厂商**默认钥 | `sta=0xFF` `pairwise=0` `cipher=TKIP` `key_idx`←KDE（`rwnx_send_key_add`） |
| GTK **代码 mode 0**（`WifiKeyAdd.c`） | 覆盖成 `sta=0` `pairwise=1`（注释写「当 sta 钥装」）；成功行仍常印 `sta=255` → **以 payload dump / 实参为准** |
| 组播 DHCP 空口 | **KeyID=0**（PTK）→ AP 丢；Linux 对照 **KeyID=2** |
| `mcast=32` 等 | 固件常 **不发** |
| 串口眼睛 | `WiFi: data tx: … vif=.. staid=.. ac=.. tid=.. mcast=0|1` |

> 眼睛结论：Docs/14 基线「cfm=0 仍 KeyID=0」在 **非厂商默认装法** 下也成立 → data-2 须一次一变量，优先对照「恢复 0xFF/pairwise=0」与「当前 mode0」的空口差，禁止再盲扫矩阵。

---

## 3 · 手测 checklist（复现 KeyID=0 基线）

1. 关联课网至 `data after keys`；确认 GTK/PTK `cfm=0`。  
2. `wififw dhcp`（或等价）触发广播 Discover。  
3. 串口抄一行：`data tx: … staid=<ap_idx> mcast=1`（staid 应等于 connect 的 ap_idx，**不是** 32）。  
4. NUC 短抓：同帧 802.11 头 **KeyID=0** → 基线仍在；若偶然 KeyID=2 → 记条件（禁止当启发式改超时）。  
5. （可选）`BLOCKS:probe.cfg` 写 `mcast=32` → 预期 **不上空气**（否证 BCMC 捷径）。  
6. （可选）`arpuni=1`（可与 `mcast=` 同文件）→ 单播 ARP 空口 KeyID / AP 是否回 → Docs/19 / data-1。

**本刀绿**：文档/注释与上表一致；串口眼睛字段齐；手测能复述「STA 组播 staid=ap、空口 KeyID=0」。
