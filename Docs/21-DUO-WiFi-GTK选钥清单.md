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
