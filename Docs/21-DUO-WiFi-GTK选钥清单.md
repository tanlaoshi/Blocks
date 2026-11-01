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

## 1a · gtkfirst=1（本刀 · 串口半绿 / 空口未抓）

**单变量**：`MM_KEY_ADD` 顺序改为 **先 GTK、后 PTK**（材料仍先派生 PTK 再解 GTK；仅交付固件的先后）。  
假说：固件组播选钥挂在「先装进默认槽的那把」；现序先 PTK 导致组播首帧走 KeyID=0。

| 探针 | `BLOCKS:probe.cfg` → `gtkfirst=1` |
|---|---|
| 战场 | `WifiHandshakeInstall.c` / `WifiJoinProbe.c` |
| 判据 | 空口首帧组播 KeyID=gtk keyid → 绿；仍先 KeyID=0 → 否证 |

| 步 | 真机 |
|---|---|
| probe | ✅ `gtkfirst=1`；`key order: gtk then ptk` |
| key add | ✅ **先** `gtk sta=255 keyid=2 … cfm=0`，**后** `ptk … cfm=0` |
| dhcp | ✅ `data tx … mcast=1` DISCOVER 已发；仍无 OFFER / `decr=0` |
| 空口 KeyID | ❌ **未抓**（NUC 192.168.31.47 monitor 过热掉线，短抓未取回） |

→ 装序变量已生效；**判据未完**，须 NUC 短抓首帧 KeyID 才能绿/否证。勿 TG。
