# 21 · DUO WiFi GTK 选钥（dwifi-data-2）

> 承接 Docs/20：单播 TX MIC 已证伪；★ 转组播空口 **KeyID = 已装 GTK 的 key_idx**（课网常为 1 或 2，勿写死 2）。  
> 一次一变量。未通 → **不 TG**。

---

## 0 · 基线（§1g 空口已见）

| 项 | 值 |
|---|---|
| 组播 staid | `ap_idx`（厂商 STA 分支） |
| GTK 装法 mode0 | `sta=0xFF` `pairwise=0` `cipher=TKIP` `key_idx`←KDE |
| 空口常见 | 先 **QoS KeyID=0**（CCMP PN），随后非 QoS **KeyID=GTK**（TKIP IV） |
| Docs/14 已扫 | keyid/cipher/pairwise/len、mcast staid、ac/tid → 无 OFFER |

判绿：DHCP Discover（或等价组播）**首帧**空口 KeyID == 串口 `key add: gtk … keyid=`。

---

## 1a · gtkfirst=1（✅ 否证）

**单变量**：`MM_KEY_ADD` 顺序改为 **先 GTK、后 PTK**（材料仍先派生 PTK 再解 GTK；仅交付固件的先后）。  
假说：固件组播选钥挂在「先装进默认槽的那把」；现序先 PTK 导致组播首帧走 KeyID=0。

| 探针 | `BLOCKS:probe.cfg` → `gtkfirst=1` |
|---|---|
| 战场 | `WifiHandshakeInstall.c` / `WifiJoinProbe.c` |
| 抓包 | NUC `wlp1s0` monitor **≤12 s** → 立刻回 managed（`/tmp/duo-gtkfirst-nuc.pcap`） |
| 判据 | 空口首帧组播 KeyID=gtk keyid → 绿；仍先 KeyID=0 → 否证 |

| 步 | 真机（2026-11-01 17:11） |
|---|---|
| probe | ✅ `gtkfirst=1`；`key order: gtk then ptk` |
| key add | ✅ **先** `gtk sta=255 keyid=1 … cfm=0`，**后** `ptk … cfm=0`（本轮 KDE keyid=**1**） |
| dhcp | ✅ `data tx … mcast=1` DISCOVER 已发；仍无 OFFER / `decr=0` |
| 空口首帧 | ❌ STA=`88:00:33:77:10:99` → `ff:ff:…` **先** QoS `IV:1 KeyID 0`，**后** 非 QoS `KeyID 1` |

→ **否证「先装 GTK 就能让组播首帧走 GTK」**。装序变量已生效，空口首帧仍 KeyID=0（PTK/CCMP）。  
下一单变量另开（勿再扫装序）。

---

## 1b · 组播 tid=0xFF（非 QoS）（✅ 否证）

**单变量**：组播 `hostdesc.tid` = `0xFF`（非 QoS；对表 Linux DHCP / ipc 注释）。  
假说：§1a 空口先 QoS KeyID=0、后非 QoS KeyID=gtk → 默认组播改非 QoS 则首帧走 GTK。

| 战场 | `WifiEapolTx.c`（曾改默认；否证后**已恢复** tid=0；复现用 `probe.cfg` `tid=255`） |
|---|---|
| 抓包 | NUC ≤12 s → `/tmp/duo-tidff-nuc.pcap`；立刻回 managed |
| 判据 | 首帧 KeyID=gtk keyid → 绿；仍先 KeyID=0 → 否证 |

| 步 | 真机（2026-11-01 17:24） |
|---|---|
| 串口 | ✅ `data tx … tid=ff mcast=1`；gtk `keyid=1` |
| 空口首帧 | ❌ STA→`ff:ff` **先** `Data IV:1 KeyID 0`（本轮无 `+QoS` 字样），**后** `KeyID 1` |
| OFFER | ❌ 无 |

→ **否证「组播 tid=非 QoS 即可让首帧 KeyID=gtk」**。固件仍先发 KeyID=0，再发 KeyID=gtk。默认 tid 已回滚。

---

## 1c · noptk=1（只装 GTK）（✅ 半证 + 否证「首帧=gtk」）

**单变量**：跳过 `MM_KEY_ADD` PTK，只装 GTK（M4 仍在装钥前发出）。  
假说：空口双发里 KeyID=0 依赖已装 PTK；无 PTK 则首帧应为 gtk keyid。

| 探针 | `BLOCKS:probe.cfg` → `noptk=1` |
|---|---|
| 战场 | `WifiHandshakeInstall.c` / `WifiJoinProbe.c` |
| 抓包 | NUC ≤12 s → `/tmp/duo-noptk-nuc.pcap`；立刻回 managed |

| 步 | 真机（2026-11-01 17:47） |
|---|---|
| probe | ✅ `noptk=1`；`ptk install skipped`；**无** `key add: ptk` |
| key add | ✅ 仅 `gtk sta=255 keyid=2 … cfm=0` |
| 空口首帧组播 | ❌ **不是** KeyID=gtk：QoS 明文 `LLC/SNAP IPv4 DHCP Discover`（无 IV/KeyID） |
| KeyID=0 | ✅ **消失**（无 PTK 则不再先发 KeyID=0） |
| OFFER | ❌ 无（AP 对关联 STA 明文数据多半丢） |

→ **证实**：空口 KeyID=0 路径依赖我们装过的 PTK。  
→ **否证**：只装 GTK **不会**让组播首帧走 KeyID=gtk；固件改为发**明文** Discover。  
→ GTK 作「默认组播钥」在仅装 GTK 时仍未接管加密 TX。下一刀勿再扫 noptk；查「有 PTK 时为何先 pairwise 再偶发 GTK」或厂商 default-key 绑定。

---

## 1d · gtkreadd=1（两钥后再装一次 GTK）（✅ 否证）

**单变量**：默认序 PTK→GTK 都成功后，**再** `MM_KEY_ADD` 一次同一 GTK。  
厂商 `rwnx_cfg80211_set_default_key` 为空操作；假说：固件组播选钥跟「最后一次默认钥（pairwise=0）ADD」走，末次 GTK 可压过「先发 KeyID=0」。

| 探针 | `BLOCKS:probe.cfg` → `gtkreadd=1`（勿叠 noptk/gtkfirst） |
|---|---|
| 战场 | `WifiHandshakeInstall.c` / `WifiJoinProbe.c` |
| 抓包 | NUC ≤12 s → `/tmp/duo-gtkreadd-nuc.pcap`；立刻回 managed |
| 判据 | 首帧 KeyID=gtk keyid → 绿；仍先 KeyID=0 → 否证 |

异于 §1a：§1a 是「先 GTK 后 PTK」；本刀是「PTK、GTK、**再 GTK**」。

| 步 | 真机（2026-11-01 17:57） |
|---|---|
| probe | ✅ `gtkreadd=1`；`gtk readd after both keys` |
| key add | ✅ `ptk … cfm=0` → `gtk keyid=2 … cfm=0` → **再** `gtk keyid=2 … cfm=0` |
| dhcp | ✅ `DISCOVER tx`；无 OFFER |
| 空口首帧 | ❌ STA→`ff:ff` **先** QoS `KeyID 0`，**后** 非 QoS `KeyID 2`（`/tmp/duo-gtkreadd-nuc.pcap`） |

→ **否证「末次默认钥 ADD=GTK 即可让组播首帧走 KeyID=gtk」**。有 PTK 时固件仍先发 KeyID=0。  
下一刀勿再扫 gtkreadd；候选：装完两钥后 `MM_KEY_DEL` PTK（异于从未装 PTK 的 noptk），或另寻 default-key 绑定证据后再动 data-3。
