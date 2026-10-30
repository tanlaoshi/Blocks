# Duo WiFi · 关联（SM_CONNECT）与 WPA2 通路调研

> 目的：回答"从能扫描到能发起关联"这一刀要抄什么、要重写什么，以及下一刀 WPA2 四次握手
> 的每块料（PTK / MIC / GTK / 装钥匙 / EAPOL 收发）到底能不能直接用 X64 那份实现。
>
> 依据（行号以本仓为准）：
> - 厂商 D80：`Source/DUO/sdk/duo-buildroot-sdk-v2-2.0.1/linux_5.10/drivers/net/wireless/aicsemi/aic8800/aic8800_fdrv/`
>   的 `lmac_msg.h`、`lmac_mac.h`、`rwnx_msg_tx.c`、`rwnx_msg_rx.c`、`rwnx_main.c`、`rwnx_rx.c`、
>   `rwnx_tx.c`、`aicwf_sdio.c`、`aicwf_sdio.h`、`aicwf_txrxif.c/.h`；
> - 本仓 X64：`Kernel/HAL/X64/Drivers/Model/Iwl/{Mac/Assoc,Mac/Eapol,Crypto,Hw/IwlMvm,Hw/IwlTx}`；
> - 本仓内核：`Kernel/HAL/RiscV/Board/milk-v-duo-s/Sdhci/`。
>
> 本刀边界：**只到"发起关联"**（`SM_CONNECT_REQ` → `CFM` → `IND`）。
> 四次握手、EAPOL 收发、`MM_KEY_ADD` **不在本刀代码里**，只做通路调研 + 留 TODO。

---

## 0 · 结论速览

| 问题 | 答案 |
|---|---|
| 关联怎么发起 | 一条 LMAC 消息 `SM_CONNECT_REQ = 0x1800`，参数 `sizeof = 320`。**主机不发 auth/assoc 帧**，LMAC 自己发 |
| 为什么不用自己拼帧 | X64 那套（`IwlBuildMgmt` + `IwlSendFrameRaw`）是"主机当软 MAC"；AIC 是 fullmac 固件，auth/assoc 由 SM 任务代劳 |
| CFM 与 IND 的分工 | `SM_CONNECT_CFM(0x1801)` 只有 1 B `status`：**0 = 受理**；随后 `SM_CONNECT_IND(0x1802)` 的 `status_code` 才是空口结果 |
| 目标从哪来 | 最近一次 `wififw scan` 的合并表（`AIC_SCAN_AP`）；所以 `join` 必须先 `scan` |
| vif 从哪来 | 复用扫描时 `MM_ADD_IF_CFM` 给的 `inst_nbr`（同一 vif）；没有就补一条 ADD_IF |
| EAPOL 怎么收 | **不是消息**，是 SDIO **数据帧**（`data[2] & 0x10 != 0x10`，`SDIO_TYPE_DATA=0x00`），整帧里是 `hw_rxhdr` + 802.11 data；SNAP `AA AA 03 .. 88 8E` |
| EAPOL 怎么发 | **没有对应的 LMAC 消息**，只能走数据 TX（`txdesc`：eth dest/src/ethertype/staid/tid/vif_idx/packet_len）；这是下一刀最大工作量 |
| 钥匙怎么交给固件 | `MM_KEY_ADD_REQ = 0x0024`，`sizeof = 44`；PTK 用 `sta_idx = IND.ap_idx`、`pairwise=1`；GTK 用 `sta_idx=0xFF`、`key_idx=KeyID` |
| PTK / MIC / GTK 能不能复用 | **算法能**（PRF-384 / HMAC-SHA1 / AES-unwrap / PBKDF2 都是纯函数）；**包装要解耦**（`gIwlMac`/`gIwlBssid`/`gIwlGroupCipher`/`IwlLogStage`） |
| 放哪 | 纯算法现在住在 `HAL/X64/**`，RISC-V 板级不能 include；下一刀把纯 `.c` 逐字搬到 `Board/milk-v-duo-s/WifiCrypto/`（不动 `Kernel/Library/**`） |
| 本刀不做什么 | 4-way handshake、`MM_KEY_ADD`、RSN IE 进 assoc（`ie_len` 仍为 0） |

---

## 1 · 「可复用 vs 要重写」对照表

判定四档：
**搬**＝纯算法/纯字节构造，函数体可以逐字复制；
**改签名搬**＝算法纯但读了全局，参数化后搬；
**重写**＝另一套总线/消息语义，只能按 AIC 重写；
**不适用**＝AIC 固件已经做了，主机不需要。

### 1.1 关联本体

| 功能 | X64 怎么做（文件:行） | AIC 对应物 | 判定 |
|---|---|---|---|
| 关联入口 | `IwlAssocRun` `Mac/Assoc/Assoc.c:10` | `SM_CONNECT_REQ(0x1800)`，参数 `struct sm_connect_req`（`lmac_msg.h:2215-2241`，320 B） | **重写**（一条消息替掉整段建帧/收帧） |
| 目标 BSS 选择 | `gIwlTarget`（`Mac/Private.h:13-25` 的 `IWL_BSS`），扫描时填好 | 扫描合并表 `AIC_SCAN_AP`（`WifiScanPrivate.h:204-212`）+ 本刀新增 `AicScanReportFind`（`WifiScanReport.c`） | **重写**（结构不同：AIC 不需要在 join 时再带 RSN/rates） |
| Open 认证帧 | `Assoc.c:39-57` 组 Auth（subtype 0xB）→ `IwlBuildMgmt` `Assoc/Util.c:14` → `IwlSendFrameRaw` | LMAC SM 任务自己发；主机只填 `auth_type=0` | **不适用** |
| Assoc 请求帧 | `Assoc.c:62-120`：caps/rates/ext-rates/HT/RSN 逐 IE 拼 | 同上；附加 IE 走 `sm_connect_req.ie_len/ie_buf`（`rwnx_msg_tx.c:2421-2425`） | **不适用**（本刀 `ie_len=0`） |
| Auth/Assoc 应答等待 | `IwlAssocWaitAuth` `Assoc/Wait.c:7`、`IwlAssocWaitResp` `Assoc/Wait.c:82`；状态码在 `Wait.c:124-146`，AID 在 `Wait.c:129` | 固件回 `SM_CONNECT_IND`：`status_code@0`、`bssid@2`、`aid@820`（`lmac_msg.h:2253-2286`） | **重写**（不再解析 802.11 帧，只解析消息） |
| Assoc 成败判据 | `Wait.c:124-131`：`Dot[26..27]==0` 才算成功，随后取 AID | `IND.status_code == 0`；AID 取 `IND.aid & 0x3fff` | **重写**（判据字段变了，语义一样） |
| 收包队列（Hold/live） | `IwlRxTake` / `IwlRxTakeHeld`（`Mac/Assoc/Util.c:33`、`Mac/Eapol/Io.c:125-127`） | AIC 的消息队列就是 SDIO RX FIFO；用 `AicReceiveFrameTimeout`（`WifiFrame.c:88`） | **重写** |
| RSN IE 构造（下一刀） | `IwlBuildStaRsn` `Mac/Assoc/Rsn.c:7-138`（22 B，PSK-only，MFPR 清） | 同一份 22 B 放进 `ie_buf` + `ie_len=22`；厂商走 `sme->ie` | **改签名搬**（纯字节构造，但读 `gIwlTarget`/`gIwlGroupCipher`/写 `gIwlStaRsn`） |
| 失败原因文案 | 状态码打在 `assoc=stXXXX`（`Wait.c:132-146`） | 802.11 status code 表（本刀 `JoinStatusText`，802.11-2016 Table 9-46） | **搬**（纯查表） |

### 1.2 EAPOL 收

| 功能 | X64 怎么做（文件:行） | AIC 对应物 | 判定 |
|---|---|---|---|
| 等 msg1 | `IwlEapolWaitMsg1` `Mac/Eapol/M1.c:7-38` | 数据帧到达时机由 AP 决定；没有"等某条消息"的说法 | **重写**（轮询方式不同） |
| 从 RX 里挑 EAPOL | `IwlEapolM1DrainRx` `Eapol/M1Poll.c:4-123`：`IwlRxTake` → 只看 `IWL_RX_MPDU_CMD` → `IwlFindEapol` | SDIO 数据帧（`aicwf_sdio.h:66` `SDIO_TYPE_DATA=0x00`）→ `hw_rxhdr` → 802.11 MPDU → SNAP | **改签名搬**（`IwlFindEapol` 本身纯，见下） |
| 在 802.11 data 里找 SNAP 0x888e | `IwlFindEapol` `Eapol/Util.c:54-104`；头长计算 `IwlDot11DataHdrLen` `Util.c:32-51`；`IwlAddr1IsUs` `Util.c:107` | 同一份逻辑可用；注意固件可能已经去掉 802.11 头（见 §7.1 的待校准点） | **搬**（纯函数，只读参数） |
| 收帧整帧读取 | `IwlRxPoll`/`IwlRxTake` 走 MMIO 队列 | `AicReceiveFrameTimeout` 已能读 1600 B 整帧（`WifiFrame.c:19-61`） | **重写**（复用本仓已有函数，不搬 X64） |
| 802.11→Ethernet 转换 | 内核 `rwnx_rx.c:2100-2160` 那段（AIC 驱动自己做） | 我们**不需要完整转换**：只在 MPDU 里定位 SNAP/0x888e，把 EAPOL 体拷出来 | **重写**（只做 EAPOL 需要的部分） |

### 1.3 EAPOL 发

| 功能 | X64 怎么做（文件:行） | AIC 对应物 | 判定 |
|---|---|---|---|
| 组 EAPOL 帧 | `IwlSendEapol` `Eapol/Io.c:177-208`：24 B 802.11 data + LLC/SNAP（`AA AA 03 00 00 00 88 8E`）+ EAPOL 体 | 若数据通路收 **Ethernet 帧**，则只需 14 B eth + EAPOL 体；若收 802.11，则同 X64 | **搬**（纯字节构造；先按 §7.1 的结论定头） |
| 发一帧 | `IwlSendFrameRaw` `Hw/IwlTx/IwlTxFrame.c:8-...`：填 `IWL_CMD_TX(0x1c)` TFD + 速率/队列，写 AUX/AP 队列 | **无 LMAC 消息**；走 `rwnx_start_xmit`（`rwnx_tx.c:1379`）→ `txdesc_api`（`rwnx_tx.c:1497-1530`）→ `aicwf_sdio_bus_txdata`（`aicwf_sdio.c:1370`）→ `aicwf_sdio_txpkt`（`:1064`，512 对齐）→ `aicwf_sdio_send_pkt`（`:185`，`sdio_writesb` 到 `wr_fifo`） | **重写**（AIC 数据 TX 本仓完全没有） |
| 速率/队列选择 | `IwlTxFrame.c:40-77`（1M/6M、q4/AP_STA） | 由 `txdesc.host` 的 `staid/tid/flags` 决定；固件按 TID 映射 AC | **重写** |
| M2/M4 重传 | `Hand.c:141`（M2 在 i==1000/2000 重发） | 同样需要，但本刀不做 | — |

### 1.4 算法与密钥材料

| 功能 | X64 实现 | 纯不纯 | 判定 |
|---|---|---|---|
| PRF-384（PTK，WPA2 默认） | `IwlPrf384` `Eapol/Crypto.c:7-38` | 纯（只调 `IwlHmacSha1`） | **搬** |
| PTK 拼装 | `IwlBuildPtk` `Crypto.c:40-77` | 读 `gIwlMac`/`gIwlBssid`（`:54-60`） | **改签名搬** |
| KDF-SHA256（KeyVer=3 / WPA3） | `IwlBuildPtk` `:72-73` → `IwlKdfSha256` `Crypto/IwlSha256.c:181` | 纯 | **搬**（本刀不用；WPA2 走 PRF-384） |
| PBKDF2-SHA1（PSK→PMK） | `IwlPbkdf2Sha1` `Crypto/IwlPbkdf.c:73`（`Hand.c:60` 调） | 纯（只调 `IwlHmacSha1`） | **搬**（下一刀先用它） |
| HMAC-SHA1 | `IwlHmacSha1` `Crypto/IwlSha1.c:118` | 纯 | **搬**（PRF/MIC/PBKDF2 的共同底座） |
| AES-CMAC（KeyVer=3 的 MIC） | `IwlAesCmac` `Crypto/IwlAes.c:156` | 纯 | **搬**（WPA2 不用，留着） |
| AES key unwrap（GTK 解密） | `IwlAesUnwrap` `Iwl/IwlWrap.c:112` | 纯 | **搬** |
| EAPOL MIC | `IwlEapolMic` `Crypto.c:80-97` | 算法纯，但**原地**清/写 `Eapol[81..96]`（魔法数） | **改签名搬**（算与填分开、偏移具名） |
| GTK 解包 | `IwlInstallGtk` `Crypto.c:99-199` | 算法纯，但写 4 个全局 + `IwlLogStage` 打印 + 读 `gIwlGroupCipher`（`:157`） | **改签名搬**（输出结构 + 返回码） |
| CCMP/TKIP 加解密 | `IwlCcmpDecrypt` `Crypto/IwlCcmp.c:232` / `IwlTkipDecrypt`（`Iwl/Private.h:30-34`） | 纯 | **搬**（下一刀装好钥匙、开始收加密数据帧时才用；M3 的 Key Data 是 KEK/AES-unwrap，不走这里） |

### 1.5 装钥匙

| 功能 | X64 怎么做 | AIC 对应物 | 判定 |
|---|---|---|---|
| 把 PTK 给固件 | `IwlStaKeysInstall` `Hw/IwlMvm/IwlMvmAssoc.c:100-120` → `IwlAddStaKey` `:84-96` → `IWL_CMD_ADD_STA_KEY` | `MM_KEY_ADD_REQ(0x0024)`/`CFM(0x0025)`，`sizeof=44`（§5） | **重写**（消息不同，字段语义一一对应） |
| 组播钥匙 | X64 选择**不给**固件（`:105-118` 注释：固件会用错 GTK） | AIC 可以给：`sta_idx=0xFF`、`key_idx=KeyID`、`pairwise=0`；但也允许主机自己解 | **重写**（本刀不做，下一刀决定给不给） |
| `sta_idx` 从哪来 | `IWL_AP_STA_ID`（X64 自己 ADD_STA 建的） | `SM_CONNECT_IND.ap_idx`（LMAC 分配，`rwnx_msg_rx.c:747-752`） | **重写**（但本刀已经把 `ap_idx` 打出来） |

---

## 2 · 消息 id 与结构体长度：全部编译取值

### 2.1 怎么取（可复现）

厂商头文件的枚举**不手数**：写一个宿主小程序，include 厂商头，直接打印枚举值与 `sizeof/offsetof`。
最小 linux 桩只需要 4 个头（`version.h`/`types.h`/`bits.h`/`bitops.h`），`LINUX_VERSION_CODE` 取 4.4
（< 4.19 ⇒ `lmac_mac.h:515` 不会去 include `linux/ieee80211.h`）：

```c
/* stub/linux/version.h */ #define KERNEL_VERSION(a,b,c) (((a)<<16)+((b)<<8)+(c))
/*                        */ #define LINUX_VERSION_CODE KERNEL_VERSION(4,4,0)
/* stub/linux/types.h   */ #include <stdint.h>、<stddef.h>、<stdbool.h>；typedef u8/u16/u32/s8/...；
/*                        */ #define __packed __attribute__((packed))
/* stub/linux/bits.h    */ #define BIT(n) (1UL << (n))

/* probe.c */
#include <stdio.h>
#include <stddef.h>
#include "lmac_msg.h"
#define PR(n, v) printf("%-32s = 0x%04x\n", n, (unsigned)(v))
#define SZ(n, t) printf("sizeof(%s) = %u\n", n, (unsigned)sizeof(t))
#define OFF(t, f) printf("  offsetof %-22s = %u\n", #f, (unsigned)offsetof(t, f))
int main(void) {
    PR("SM_CONNECT_REQ", SM_CONNECT_REQ);  PR("SM_CONNECT_CFM", SM_CONNECT_CFM);
    PR("SM_CONNECT_IND", SM_CONNECT_IND);  PR("MM_KEY_ADD_REQ", MM_KEY_ADD_REQ);
    PR("MM_KEY_ADD_CFM", MM_KEY_ADD_CFM);
    SZ("sm_connect_req", struct sm_connect_req);
    SZ("sm_connect_cfm", struct sm_connect_cfm);
    SZ("sm_connect_ind", struct sm_connect_ind);
    SZ("mm_key_add_req", struct mm_key_add_req);
    SZ("mm_key_add_cfm", struct mm_key_add_cfm);
    SZ("mac_sec_key", struct mac_sec_key);
    OFF(struct sm_connect_req, ie_buf);
    OFF(struct sm_connect_ind, ap_idx);
    OFF(struct sm_connect_ind, center_freq);
    OFF(struct sm_connect_ind, aid);
    OFF(struct mm_key_add_req, key);
    OFF(struct mm_key_add_req, cipher_suite);
    OFF(struct mm_key_add_req, pairwise);
    return 0;
}
```

编译：`gcc -std=c99 -DCONFIG_RWNX_FULLMAC -I stub -I <aic8800_fdrv> probe.c -o probe`
（`CONFIG_RWNX_FULLMAC` 必须定义，否则 `lmac_msg.h:63` 直接 `#error`。）

**完整 probe 的原始输出**（本刀实际跑的就是它）：

```text
--- task / msg ids (counted by compiler from vendor enum) ---
TASK_MM                            = 0x0000 (0)
TASK_SCANU                         = 0x0004 (4)
TASK_ME                            = 0x0005 (5)
TASK_SM                            = 0x0006 (6)
MM_RESET_REQ                       = 0x0000 (0)
MM_START_REQ                       = 0x0002 (2)
MM_ADD_IF_REQ                      = 0x0006 (6)
MM_ADD_IF_CFM                      = 0x0007 (7)
MM_KEY_ADD_REQ                     = 0x0024 (36)
MM_KEY_ADD_CFM                     = 0x0025 (37)
MM_KEY_DEL_REQ                     = 0x0026 (38)
MM_KEY_DEL_CFM                     = 0x0027 (39)
SM_CONNECT_REQ                     = 0x1800 (6144)
SM_CONNECT_CFM                     = 0x1801 (6145)
SM_CONNECT_IND                     = 0x1802 (6146)
SM_DISCONNECT_REQ                  = 0x1803 (6147)
SM_DISCONNECT_CFM                  = 0x1804 (6148)
SM_DISCONNECT_IND                  = 0x1805 (6149)
SCANU_START_REQ                    = 0x1000 (4096)
SCANU_RESULT_IND                   = 0x1004 (4100)

--- sizes ---
sizeof(sm_connect_req)             = 320
sizeof(sm_connect_cfm)             = 1
sizeof(sm_connect_ind)             = 852
sizeof(sm_disconnect_req)          = 4
sizeof(mm_key_add_req)             = 44
sizeof(mm_key_add_cfm)             = 2
sizeof(mm_key_del_req)             = 1
sizeof(mac_sec_key)                = 36
sizeof(mac_ssid)                   = 33
sizeof(mac_addr)                   = 6
sizeof(mac_chan_def)               = 6
sizeof(lmac_msg)                   = 8
```

**镜像自检**：本刀新增的 `WifiAssocMessage.c` 里 `AicJoinLayoutCheck()` 把上面 36 项
（4 个 sizeof + 32 个 offsetof）全部再核一遍，任何一项对不上就拒绝发送。这 36 项在宿主
（gcc 64 位）和 RISC-V 上都必须一致 —— 镜像里没有指针、没有 `long`，只有定宽类型 + 显式填充，
所以与目标位宽无关；`WifiConfigMessage.c` 的 `AicBringupLayoutCheck()` 用的是同一套办法。

### 2.2 与本刀代码的对应

| 名字 | 值 | 本仓宏 | 厂商出处 |
|---|---|---|---|
| `TASK_SM` | 6 | `AIC_TASK_SM` | `lmac_msg.h:46`（task 枚举第 7 项，MM=0 起） |
| `SM_CONNECT_REQ` | `0x1800` | `AIC_SM_CONNECT_REQ` | `lmac_msg.h:2185` = `LMAC_FIRST_MSG(TASK_SM)`（0x1800） |
| `SM_CONNECT_CFM` | `0x1801` | `AIC_SM_CONNECT_CFM` | `lmac_msg.h:2187` |
| `SM_CONNECT_IND` | `0x1802` | `AIC_SM_CONNECT_IND` | `lmac_msg.h:2189` |
| `SM_DISCONNECT_IND` | `0x1805` | `AIC_SM_DISCONNECT_IND` | `lmac_msg.h:2195` |
| `MM_KEY_ADD_REQ` | `0x0024` | `AIC_MM_KEY_ADD_REQ` | `lmac_msg.h:210`（`mm_msg_tag` 第 36 项） |
| `MM_KEY_ADD_CFM` | `0x0025` | `AIC_MM_KEY_ADD_CFM` | `lmac_msg.h:212` |

> 注意：上一份 MM_START 调研里 `MM_SET_RF_CALIB=0x69` 这类是**同一张 mm_msg_tag 表**的
> 第 105 项左右，与这里的 0x24 不矛盾 —— 表是连续的，0x24 在 0x69 之前。

---

## 3 · `struct sm_connect_req` 字段表（本刀实际填的值）

偏移/大小来自 §2 的编译输出；"来源"栏区分 **抄**（厂商代码/头文件直接读到）与
**推**（按默认值/对齐推出来的，已在宿主和 RISC-V 两端编译验证）。

| 偏移 | 大小 | 字段（厂商名） | 本刀值 | 来源 | 说明 |
|---|---|---|---|---|---|
| 0 | 1 | `ssid.length` | `strlen(ssid)`（≤32） | 抄 `rwnx_msg_tx.c:2419` | 扫描缓存的 SSID 去掉结尾 0 |
| 1 | 32 | `ssid.array[32]` | SSID 原始字节 | 抄 `:2417-2418` | 不保证结尾 0 |
| 33 | 1 | *填充* | 0 | 推（对齐 `mac_addr` 的 u16） | 宿主/目标都验过 offset=34 |
| 34 | 6 | `bssid`（`mac_addr`） | 扫描缓存 BSSID | 抄 `:2405-2408` | 按字节写（小端 = 线上顺序） |
| 40 | 2 | `chan.freq` | 目标频率 2412..2472 | 抄 `:2410-2414` | 由扫描结果的 `Frequency` 直接给 |
| 42 | 1 | `chan.band` | 0（`PHY_BAND_2G4`） | 抄 `:2411` + `lmac_mac.h:69` | |
| 43 | 1 | `chan.flags` | 0 | 抄 `:2413`（`get_chan_flags` 无标志时=0）+ `lmac_mac.h:99-107` | 不加 NO_IR/DISABLED/RADAR |
| 44 | 1 | `chan.tx_power` | 0 | 推（厂商 zalloc 后只填前三个字段） | 真机若因功率=0 拒，再补 20 |
| 45 | 1 | *填充* | 0 | 推（`mac_chan_def` 对齐 2） | |
| 46 | 2 | *填充* | 0 | 推（对齐 u32 `flags`） | |
| 48 | 4 | `flags` | `0x00000000` | 抄 `:2420`（open 时 `flags` 保持 0） | `CONTROL_PORT_HOST=BIT(0)` 留给下一刀 |
| 52 | 2 | `ctrl_port_ethertype` | `0x888e` | 抄 `:2403` 的语义（EAPOL/`ETH_P_PAE`），**值是本刀主动填的** | 提前声明"EAPOL 归主机" |
| 54 | 2 | `ie_len` | 0 | 抄 `:2425` | 下一刀放 RSN IE 时改 22 |
| 56 | 2 | `listen_interval` | 0 | 抄 `:2426` + `rwnx_mod_params.c:59`（默认 0） | |
| 58 | 1 | `dont_wait_bcmc` | 0 | 推 `:2427`（`!listen_bcmc`，默认 `listen_bcmc=true`） | |
| 59 | 1 | `auth_type` | 0（`WLAN_AUTH_OPEN`） | 抄 `:2431/2433` + `linux/ieee80211.h:2481` | 本刀只做开放认证 |
| 60 | 1 | `uapsd_queues` | 1（`AC_VO`） | 抄 `:2444` + `rwnx_mod_params.c:66` + `ieee80211.h:281` | |
| 61 | 1 | `vif_idx` | 扫描时 `MM_ADD_IF_CFM.inst_nbr` | 抄 `:2409` | **必须与扫描同一个 vif** |
| 62 | 2 | *填充* | 0 | 推（对齐 u32 `ie_buf`） | |
| 64 | 256 | `ie_buf[64]` | 全 0 | 抄 `:2421-2424`（`ie_len=0` 时厂商也不填） | 下一刀：RSN IE 从这里开始 |
| **320** | | **sizeof** | | 编译 | `16 B` 消息头 + 320 = 336 → FIFO 补齐 512 |

> 为什么 `ie_len=0` 也能连 WPA2 AP：open 认证的 assoc 请求里本来就没有 RSN IE；AP 会先接受
> 关联、再开 802.1X。真正决定"能不能过"的是后面的 EAPOL，不是这个字段。

---

## 4 · `struct sm_connect_ind` 字段表与失败原因

`sizeof=852`（到 `ac_param[4]` 末尾）。本刀只读加粗的那几个；其余打印出来是为了排障。

| 偏移 | 大小 | 字段 | 本刀用途 |
|---|---|---|---|
| **0** | 2 | `status_code` | **成败判据**（0 = 关联成功） |
| **2** | 6 | `bssid` | **实际连上的 BSSID**（可能不是请求里那个，AP 可能换） |
| 8 | 1 | `roamed` | 漫游触发时置位；正常应是 0 |
| 9 | 1 | `vif_idx` | 应等于请求里的 vif |
| **10** | 1 | `ap_idx` | **下一刀 `MM_KEY_ADD.sta_idx` 就用它** |
| 11 | 1 | `ch_idx` | LMAC 信道上下文号 |
| 12 | 1 | `qos` | AP 支持 WMM |
| 13 | 1 | `acm` | WMM ACM 位 |
| 14 | 2 | `assoc_req_ie_len` | 打印（核对 assoc 请求 IE 长度） |
| 16 | 2 | `assoc_rsp_ie_len` | 打印（AP 的 assoc 响应 IE 长度） |
| 18 | 2 | *填充* | 对齐 u32 |
| 20 | 800 | `assoc_ie_buf[200]` | 本刀不解析（下一刀要 RSN/WMM 时再说） |
| **820** | 2 | `aid` | 打印（低 14 位有效） |
| 822 | 1 | `band` | 打印 |
| 823 | 1 | *填充* | 对齐 u16 |
| **824** | 2 | `center_freq` | **信道**：`ch = (freq-2412)/5 + 1`（2.4G） |
| 826 | 1 | `width` | 打印（0=20MHz） |
| 827 | 1 | *填充* | 对齐 u32 |
| 828 | 4 | `center_freq1` | 本刀只登记 |
| 832 | 4 | `center_freq2` | 本刀只登记 |
| 836 | 16 | `ac_param[4]` | 本刀只登记（EDCA 参数，下一刀 TX 可能用） |

### 4.1 `status_code` 文案（本刀 `JoinStatusText`）

| 码 | 文案 | 出处 |
|---|---|---|
| 0 | success | 802.11-2016 Table 9-46 |
| 1 | unspecified failure | 同上 |
| 10 | AP cannot support all requested capabilities | 同上（RSN/HT 能力不匹配时常见） |
| 11 | reassociation denied (AP cannot confirm the association) | 同上 |
| 12 | association denied (reason outside this standard) | 同上 |
| 13 | AP does not support all data rates in the request | 同上 |
| 17 | AP cannot handle more associated STAs | 同上 |
| 18 | STA does not support all data rates required by the BSS | 同上 |
| 其它 | `unknown 802.11 status`（原始 hex 已打） | 不猜 |

### 4.2 为什么一定要 IND 才判成功

厂商 `rwnx_send_msg(..., reqcfm=1, SM_CONNECT_CFM, cfm)`（`rwnx_msg_tx.c:2467`）只等 CFM；
`sm_connect_cfm.status` 的注释（`lmac_msg.h:2246-2248`）明确写：**0 只表示"接下来会做"，
结果由后续 `SM_CONNECT_IND` 给出**。厂商 `rwnx_rx_sm_connect_ind`（`rwnx_msg_rx.c:721-946`）
也是拿 `ind->status_code` 去调 `cfg80211_connect_result`（`:922`），不是 CFM。

---

## 5 · `MM_KEY_ADD_REQ` 字段表（下一刀用，本刀不发）

`sizeof(mm_key_add_req) = 44`。结构体定义：`lmac_msg.h:797-812`；`struct mac_sec_key`
`lmac_mac.h:292-297`；`MM_KEY_ADD_REQ` 枚举位置 `lmac_msg.h:210`。

| 偏移 | 大小 | 字段 | 下一刀填什么 | 来源 |
|---|---|---|---|---|
| 0 | 1 | `key_idx` | PTK：0（pairwise 时不用）；GTK：`KeyID`（0..3） | 抄 `rwnx_msg_tx.c:661` |
| 1 | 1 | `sta_idx` | PTK：`IND.ap_idx`；GTK：`0xFF`（default key） | 抄 `:655-662` + `rwnx_main.c:3678`（`sta ? sta->sta_idx : 0xFF`） |
| 2 | 2 | *填充* | 0 | 推（对齐 `key.array` 的 u32） |
| 4 | 1 | `key.length` | 16（CCMP） | 抄 `:665` |
| 5 | 3 | *填充* | 0 | 推（对齐 u32） |
| 8 | 32 | `key.array[8]`（u32） | 16 B PTK/GTK，按字节拷贝 | 抄 `:666`（`memcpy(&key.array[0], key, key_len)`） |
| 40 | 1 | `cipher_suite` | PTK/GTK 都是 `MAC_CIPHER_CCMP = 2` | 抄 `rwnx_main.c:3653-3656` + `lmac_mac.h:145` |
| 41 | 1 | `inst_nbr` | vif_idx（与 join/scan 同一个） | 抄 `rwnx_msg_tx.c:664` |
| 42 | 1 | `spp` | 0（A-MSDU SPP） | 推（厂商 zalloc 后不填） |
| 43 | 1 | `pairwise` | PTK：1；GTK：0 | 抄 `:663` |
| **44** | | **sizeof** | | 编译 |

### 5.1 `cipher_suite` 取值（`enum mac_cipher_suite`，`lmac_mac.h:139-168`）

| 值 | 名字 | 说明 |
|---|---|---|
| 0 | `MAC_CIPHER_WEP40` | |
| 1 | `MAC_CIPHER_TKIP` | |
| **2** | **`MAC_CIPHER_CCMP`** | WPA2-PSK 默认，本刀之后的 PTK/GTK 都用它 |
| 3 | `MAC_CIPHER_WEP104` | |
| 5 | `MAC_CIPHER_BIP_CMAC_128` | 802.11w 组播管理帧 |

厂商映射在 `rwnx_main.c:3645-3682`：`WLAN_CIPHER_SUITE_CCMP → MAC_CIPHER_CCMP`。

### 5.2 钥匙从哪来（X64 对照）

| 钥匙 | X64 存哪 | 长度 | 装法 |
|---|---|---|---|
| PMK | `gIwlPmk`（`Eapol/Hand.c:53-64` 由 PSK+SSID PBKDF2 得） | 32 | 不进固件 |
| PTK | `Ptk[48]`（`Hand.c:69` `IwlBuildPtk`） | 48 | `KCK=Ptk[0..15]`、`KEK=Ptk[16..31]`、`TK=Ptk[32..47]`（`Hand.c:70-71`）；**给固件的是 TK 这 16 B** |
| GTK | `gIwlGtk[32]`/`gIwlGtkId`（`Crypto.c:141-161`，AES-unwrap 后解析 KDE） | 16（CCMP） | `key_idx = KeyID`、`sta_idx=0xFF`、`pairwise=0` |

> X64 的做法（`IwlMvmAssoc.c:105-118`）：只把 PTK 给固件；组播保留 `DIS_GRP_DECRYPT`、主机自己解。
> AIC 这边也可以两选一，下一刀先用"只给 PTK"（风险最小，和 X64 已验路径一致）。

---

## 6 · PTK 派生 / MIC / GTK 解密：能不能直接用 X64 的算法

### 6.1 能，但要分两层看

**第一层：纯原语（7 个，全部可逐字搬）**

| 原语 | 位置 | 依赖 | 备注 |
|---|---|---|---|
| `IwlHmacSha1` | `Crypto/IwlSha1.c:118` | 无（`IwlSha1` `:91`） | PRF/MIC/PBKDF2 的底座 |
| `IwlPrf384` | `Eapol/Crypto.c:7` | 只调 `IwlHmacSha1` | WPA2 PTK |
| `IwlPbkdf2Sha1` | `Crypto/IwlPbkdf.c:73` | 只调 `IwlHmacSha1` | PSK→PMK |
| `IwlAesUnwrap` | `Iwl/IwlWrap.c:112` | 无 | GTK（KEK 解 Key Data） |
| `IwlAesCmac` | `Crypto/IwlAes.c:156` | 无 | KeyVer=3 的 MIC（WPA2 不用） |
| `IwlKdfSha256` | `Crypto/IwlSha256.c:181` | 无 | KeyVer=3 的 PTK（WPA2 不用） |
| `IwlCcmpDecrypt` | `Crypto/IwlCcmp.c`（声明 `Iwl/Private.h:33`） | 无 | 需要时再搬 |

判断"纯"的标准：**不含全局、不打印、不碰 MMIO、输入输出全在参数里**。上面 7 个都满足。

**第二层：包装（3 个，算法可搬、签名要改）**

| 现函数 | 耦合点（行号） | 怎么解耦 |
|---|---|---|
| `IwlBuildPtk` `Crypto.c:40` | 读 `gIwlMac`/`gIwlBssid`（`:54-60`）做 min/max | 改成 `AicWpaBuildPtk(Pmk, Mac[6], Bssid[6], Anonce[32], Snonce[32], Ptk[48], Ver)`；min/max 逻辑原样 |
| `IwlEapolMic` `Crypto.c:80` | 原地把 `Eapol[81..96]` 清零再回填（`:84-96`，81 是魔法数） | 拆成 `AicWpaMic(Ver, Kck, Frame, Len, Mic)`（只算，不动输入）+ 调用方自己写回；`81` 起名 `AIC_EAPOL_MIC_OFFSET`（99 B 头：4 EAPOL + 95 Key 体，MIC 在 +81） |
| `IwlInstallGtk` `Crypto.c:99` | 写全局 `gIwlGtk/gIwlGtkLen/gIwlGtkId/gIwlGtkAltOk`；读 `gIwlGroupCipher`（`:157`）；用 `IwlLogStage`/`HalSerialFormatHex` 打印（`:116-192`） | 改成 `AicWpaGtkParse(Eapol, Len, Kek, GroupCipher, AIC_WPA_GTK *Out)`，`Out = {Key[32], Len, Id, Alt[16], AltOk}`；日志由调用方打（本仓用 `SdhciMark`） |

**不能复用的**：`IwlSendEapol`/`IwlSendFrameRaw`/`IwlRxTake*`/`IWL_CMD_TX`/`IWL_CMD_ADD_STA_KEY`
这一整套收发与命令通道 —— AIC 是 SDIO 数据通路 + LMAC 消息，语义、帧头、队列全不同（§7）。

### 6.2 放哪里（依赖方向）

这些纯算法现在住在 `Kernel/HAL/X64/Drivers/Model/Iwl/Crypto/` 与 `.../Iwl/IwlWrap.c`。
RISC-V 板级**不能** include X64 目录（分层铁律：`Board` 只依赖 `Common/Core/Library`，
不跨架构 HAL；而且两份代码要分别用不同交叉编译器）。

本刀不动 `Kernel/Library/**`、不动 X64。下一刀的正确落点二选一：

1. **（推荐）复制到板级**：`Kernel/HAL/RiscV/Board/milk-v-duo-s/WifiCrypto/`（或 `Sdhci/` 下），
   函数体逐字不动，只改 include 与日志出口。好处：本刀允许的目录范围内就能做，diff 可审。
2. 后续单独一刀提升到 `Kernel/Library/Crypto/` 做跨架构共享。代价：要改 Library（本刀禁改），
   且需要同时改 X64 的 include —— 是独立的一刀，不该混进 WPA2。

**逐字搬的验收**：搬完对同一组输入（PSK+SSID、Anonce/Snonce、Key Data）比较输出，
与 X64 真机日志里的 `wpa2=m1/m2/m3` 结果一致；算法函数不允许"顺手优化"。

---

## 7 · EAPOL 收发路径（下一刀的接点）

### 7.1 收：从哪个 function / 哪条通路

**结论：EAPOL 不是 LMAC 消息，是 SDIO 数据帧。** 完整的厂商链路：

```text
SDIO 中断 fn1 0x04
  → aicwf_sdio_hal_irqhandler            aicwf_sdio.c:2002
  → aicwf_sdio_readframes                aicwf_sdio.c:1152 / 1185
  → aicwf_sdio_recv_pkt                  aicwf_sdio.c:197 / 218   (sdio_readsb, rd_fifo_addr)
  → aicwf_sdio_enq_rxpkt → sdio_busrx_thread   aicwf_sdio.c:1796 / 1856
  → aicwf_process_rxframes                aicwf_txrxif.c:276
      按 data[2] & 0x10 分流：
        != 0x10  → 数据帧（SDIO_TYPE_DATA=0x00）   aicwf_txrxif.c:308
        == 0x10  → 配置帧（0x11 CMD_RSP / 0x12 DATA_CFM / 0x13 PRINT）  :344-350
  → rwnx_rxdataind_aicwf                  rwnx_rx.c:1981
      hw_rxhdr（RX_HWHRD_LEN=60，aicwf_txrxif.h:28）；msdu_offset=sizeof(hw_rxhdr)+2（:1987）
  → 802.11 data → 组 Ethernet 头 → reord_process_unit   rwnx_rx.c:1723
  → EtherType==0x888e 直接绕过重排       rwnx_rx.c:1727   (ETH_P_PAE)
  → reord_single_frame_ind                rwnx_rx.c:1471
  → rwnx_rx_data_skb_forward              rwnx_rx.c:368
  → eth_type_trans / netif_receive_skb    rwnx_rx.c:410 / 420
```

帧类型常量（`aicwf_sdio.h:66-70`）：

| 值 | 名字 |
|---|---|
| `0x00` | `SDIO_TYPE_DATA`（数据帧，EAPOL 走这里） |
| `0x10` | `SDIO_TYPE_CFG` |
| `0x11` | `SDIO_TYPE_CFG_CMD_RSP`（我们现在的 CFM） |
| `0x12` | `SDIO_TYPE_CFG_DATA_CFM` |
| `0x13` | `SDIO_TYPE_CFG_PRINT` |

**我们内核的现状**：`AicReceiveFrameTimeout` 只接受 `Frame[2]&0x7f == 0x11/0x12` 且
`pattern == 0xaddede2a` 的帧（`WifiFrame.c:64-86`；`WifiMessage.c:342-358` 同理）。
数据帧在 `AicFrameCheck` 就被丢掉 —— 下一刀要加"数据帧分支"，不能改坏现有 CFM 路径。

**下一刀要校准的第一个数**：802.11 MPDU 在数据帧里的偏移。厂商代码里是
`skb_pull(skb, msdu_offset + 2)`（`rwnx_rx.c:2141`，`msdu_offset = sizeof(hw_rxhdr)+2`），
即 64；但 `hw_rxhdr` 的 `hwvect.len` 正好压在最前面 2 B（`rwnx_rx.h:230-235` 的
`len:16 + reserved:8`，注释写 "data type is included"）。**这个偏移在真机上要用原始 hexdump 对一次**
（`wififw join` 成功之后立刻 dump 第一帧数据），不要照抄 64 就写死。

### 7.2 发：用什么消息

**结论：没有"发 EAPOL"的 LMAC 消息；只能走数据 TX。** 厂商链路：

```text
netdev xmit → rwnx_start_xmit                 rwnx_tx.c:1379
  → skb_pull(14)（Ethernet 头）→ rwnx_txhdr + txdesc
     desc->host: eth_dest_addr / eth_src_addr / ethertype / staid / tid / vif_idx /
                 flags / packet_len / hostid          rwnx_tx.c:1497-1530
  → rwnx_txq_queue_skb → rwnx_hwq_process
  → aicwf_sdio_bus_txdata                      aicwf_sdio.c:1370
  → sdio_bustx_thread → aicwf_sdio_txpkt       aicwf_sdio.c:1064（512 对齐）
  → aicwf_sdio_send_pkt                        aicwf_sdio.c:185（sdio_writesb, wr_fifo_addr）
```

对本仓的含义：**下一刀要实现 AIC 的数据 TX 描述符**（本仓目前只有 CFG 消息 TX，
`AicSendMessageEx` 写的是 16 B 消息头，不是 txdesc）。这是四次握手里工作量最大的一块，
比 PTK/MIC 都大。

**`MM_KEY_ADD` 与 EAPOL 的关系**：`MM_KEY_ADD_REQ` 只把钥匙交给固件（让固件能加/解
后续数据帧），**不参与** EAPOL 收发。顺序是：收 M1 → 发 M2 → 收 M3（验 MIC）→
`MM_KEY_ADD(PTK)` → 解密 GTK → 发 M4 → （可选 `MM_KEY_ADD(GTK)`）。

### 7.3 代码里的 TODO 点

| 位置 | 内容 |
|---|---|
| `WifiAssoc.c` 文件头 | 下一刀三个函数名与接法：`AicJoinEapolPoll()` / `AicJoinEapolSend()` / `AicJoinKeyAdd()` |
| `WifiAssocReport.c` `AicJoinHandleInd()` 成功分支 | 打印 `join: TODO next knife = EAPOL(0x888e) 4-way + MM_KEY_ADD(0x0024), not here`，并把 `IND.ap_idx`（下一刀的 `sta_idx`）打在上一行 |
| `WifiAssocPrivate.h` | `AIC_MM_KEY_ADD_REQ/CFM` 已登记（本刀不发），避免下刀再数一遍 |

---

## 8 · 真机判据

> 下面的"期望输出"是**用本仓真实代码**（`WifiAssoc.c`/`WifiAssocMessage.c`/`WifiAssocReport.c`
> 原样编进宿主桩：假扫描缓存 + 假消息队列）跑出来的，除 SDIO 行外与真机一致。
> 真机上 `AicSendMessageEx` 会多打 `WiFi: tx fc=.. padded=.. blocks=..`，
> `AicReceiveFrameTimeout` 会多打 `WiFi: rx masked=.. status=.. fn=.. len=.. mode=..` 与 `rx dump`。

### 8.1 `wififw join dry`（有扫描缓存）

```text
WiFi: join dry: build only, nothing sent
WiFi: join target: ssid="Apple" bssid=04:11:22:33:44:f2 freq=2412 ch=1 rssi=-50
WiFi: join vif=3
WiFi: sm_connect_req tx len=320 (auth=open flags=0x00000000 eapol=0x888e)
WiFi:  dump[64] of 320 05 41 70 70 6c 65 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 04 11 22 33 44 f2 6c 09 00 00 00 00 00 00 00 00 00 00 8e 88 00 00 00 00 00 00 01 03 00 00
WiFi: join dry ok (1 built, 0 sent)
wififw: ok
```

逐字节核对（前 64 B）：`05`=ssid 长 5、"Apple"；`+34`=BSSID；`+40`=`6c 09`=2412（LE）；
`+48..51`=flags 0；`+52`=`8e 88`=0x888e（LE）；`+59`=auth 0；`+60`=uapsd 1；`+61`=vif。

### 8.2 `wififw join dry`（**没有**扫过：降级成通配符，仍可看布局）

```text
WiFi: join dry: build only, nothing sent
WiFi: join dry: no scan cache (run 'wififw scan' first), bssid=ff:ff:ff:ff:ff:ff freq=0
WiFi: join dry: no vif yet, vif=0 (real run would send MM_ADD_IF)
WiFi: join target: ssid="Nope" bssid=ff:ff:ff:ff:ff:ff freq=0 ch=0 rssi=0
WiFi: join vif=0
WiFi: sm_connect_req tx len=320 (auth=open flags=0x00000000 eapol=0x888e)
WiFi:  dump[64] of 320 04 4e 6f 70 65 00 ...（后 57 B 略）
WiFi: join dry ok (1 built, 0 sent)
wififw: ok
```

### 8.3 真机 `wififw join`：成功形态（开放 AP）

```text
WiFi: add if reuse (vif=3)                       ← 扫描已建过 vif；没扫过会打 "adding one (MM_ADD_IF)"
WiFi: join target: ssid="Apple" bssid=04:11:22:33:44:f2 freq=2412 ch=1 rssi=-50
WiFi: join vif=3
WiFi: sm_connect_req tx len=320 (auth=open flags=0x00000000 eapol=0x888e)
WiFi: tx fc=03 padded=0200 blocks=01           ← 真机多出来的 SDIO 行
WiFi: rx masked=.. status=.. fn=1 len=0100 mode=block
WiFi: rx pattern=addede2a ok
WiFi: join cfm ok (status=0x00, auth procedure started)
WiFi: rx ... id=1802 plen=0354 ...
WiFi: SM_CONNECT_IND: status_code=0x0000 (success) bssid=04:11:22:33:44:f2 freq=2412 ch=1 band=0 width=0
WiFi:  ind params: plen=852 vif=3 ap=1 ch_idx=0 qos=1 acm=0 roamed=0 req_ie=34 rsp_ie=40 aid=0xc003
WiFi: join ok (associated, open auth)
WiFi: join: TODO next knife = EAPOL(0x888e) 4-way + MM_KEY_ADD(0x0024), not here
wififw: ok
```

判据：出现 `join cfm ok` + `status_code=0x0000` + 最后 `wififw: ok`。
`ap=1` 就是下一刀 `MM_KEY_ADD.sta_idx` 要用的值。

### 8.4 真机 `wififw join`：三种失败形态（文案不同，必须能分开）

**A. CFM 超时**（固件没回；多半 bringup 没做 / 消息通道断）

```text
WiFi: join FAILED at step 1/2: no SM_CONNECT_CFM(0x1801) in 2000ms (firmware did not answer; bringup + scan first)
wififw: FAILED
```

**B. CFM 非 0**（固件当场拒绝，还没上空口）

```text
WiFi: join FAILED at step 1/2: SM_CONNECT_CFM status=0x01 (firmware refused before auth: busy / bad vif / unsupported param)
wififw: FAILED
```

**C. IND 报错**（空口拒绝）。**目标 AP 是 WPA2、我们只发 open 认证时，最可能看到这一条
（或 status_code=0、随后 AP 一直等 M1）**：

```text
WiFi: join cfm ok (status=0x00, auth procedure started)
WiFi: SM_CONNECT_IND: status_code=0x0011 (AP cannot handle more associated STAs) bssid=04:11:22:33:44:f2 freq=2412 ch=1 band=0 width=0
WiFi:  ind params: plen=852 vif=3 ap=1 ch_idx=0 qos=1 acm=0 roamed=0 req_ie=34 rsp_ie=40 aid=0xc003
WiFi: join FAILED at step 2/2: SM_CONNECT_IND status_code=0x0011 (AP cannot handle more associated STAs)
wififw: FAILED
```

**D. CFM 成功但 IND 不来**（第三种之外单独一条，便于区分"空口没回"）

```text
WiFi: join cfm ok (status=0x00, auth procedure started)
WiFi: join FAILED at step 2/2: no SM_CONNECT_IND(0x1802) in 3000ms (CFM ok but auth/assoc got no answer)
wififw: FAILED
```

> 说明：对 WPA2 AP 来说，"open 认证 + 无 RSN IE 的 assoc"通常仍会被接受（status_code=0），
> 之后 AP 发 M1、我们不回 → 过一会儿它自己 deauth。所以**B/C/D 三种都算"消息通了"**；
> 关键是能看到 `SM_CONNECT_IND`（或至少 `join cfm ok`）。真正的失败诊断看 §10。

---

## 9 · 本刀交付（文件 / 函数行数）

| 文件 | 行数 | 内容 |
|---|---|---|
| `Sdhci/WifiAssocPrivate.h` | 179 | 消息 id（编译取值）、`AIC_SM_CONNECT`(320)/`AIC_SM_CONNECT_CONFIRM`(1)/`AIC_SM_CONNECT_INDICATION`(852) 镜像、预算常量 |
| `Sdhci/WifiAssocMessage.c` | 159 | `AicJoinBuild`（28 行）/ `AicJoinLayoutCheck`（23 行，36 项） |
| `Sdhci/WifiAssocReport.c` | 255 | `AicJoinResolve`(16) / `AicJoinPlaceholder`(9) / `AicJoinPrintTarget`(16) / `AicJoinPrintTx`(9) / `AicJoinDump`(13) / `AicJoinHandleInd`(32) / 6 个 static 辅助（≤28 行） |
| `Sdhci/WifiAssoc.c` | 209 | `JoinWaitId`(21) / `JoinSend`(9) / `JoinWaitCfm`(24) / `JoinWaitInd`(10) / `JoinRun`(24) / `JoinDry`(37) / `SdhciWifiJoin`(38) |
| `Sdhci/WifiScan.c` | +19（339→358） | `AicScanVifCached` / `AicScanVifEnsure` |
| `Sdhci/WifiScanReport.c` | +42（336→378） | `AicScanReportFind` / `AicScanReportCount` |
| `Sdhci/WifiScanPrivate.h` | +6 | 上述四个原型 |
| `Services/Shell/Commands/Wifi.c` | 500→499 | `CommandWifiFwJoin`(22) + 一行分发 + 用法行；顺手把 20 处重复的 MarkSink 收成 `WifiMarkBegin/End`（净 -1 行，压回 500 以内） |

所有新增函数 ≤ 38 行（规范 60/80），新增 `.c` 全部 ≤ 255 行（规范 300/500）。

### 9.1 构建产物

| 项 | 值 |
|---|---|
| 构建命令 | `cd Kernel && BUILDDIR=Build/join ./build.sh riscv duo`（x86：`BUILDDIR=Build/join-x64 ./build.sh x86_64`） |
| 产物 | `Kernel/Build/join/HAL/RiscV/Kernel.elf` |
| 字节数 | **2248656** |
| md5 | **de315f49dc31bf5b0b93ac866e15a3aa** |
| 副本 | `/tmp/join.elf`（同 md5，已 cp） |
`WifiScanReport.c` 378 行、`WifiScan.c` 358 行是"存量超 300 + 本刀只追加访问器"，
职责仍是"扫描合并表"一件事，未触及 500 行硬线。

---

## 10 · 最可能卡在哪（2 条）与下一步

### 风险 1：`SM_CONNECT_CFM` 非 0，或 `IND.status_code` 报 10/12/17/18 —— 参数/状态不匹配

**为什么可能**：本刀按厂商 fullmac 路径最小化填字段，但有些前置是厂商在 probe 阶段
（`MM_SET_RF_CALIB`/`TXPWR`、MAC 地址）顺手做过的，我们只做了 bringup+scan。
尤其 `chan.tx_power=0`、`flags=0`、没有 `MM_SET_VIF_STATE`（整个 aic8800_fdrv 目录里
`MM_SET_VIF_STATE_REQ` 只出现在 `rwnx_strs.c` 的名字表，没有任何发送点）—— 任何一个都
可能让 SM 任务拒。

**下一步（按顺序试，一次只改一个变量）**：
1. 先看 `CFM status` 的具体值：厂商把它当 `CO_*` 用（`lmac_msg.h:88-102`：`CO_OK=0`、
   `CO_FAIL=1`…`CO_BUSY=8`、`CO_OP_IN_PROGRESS=9`；`rwnx_main.c:3891-3903` 的 switch）。
   若拿到 8/9 ⇒ 状态机没准备好，重试或先 `mmreset`；拿到 1/4 ⇒ 参数被拒，按 2/3 步改；
2. 若 CFM=0、IND 非 0：把 `chan.tx_power` 改成 bringup 同款 20，再试；
3. 若仍 10/12：把 `flags |= CONTROL_PORT_HOST`（`AIC_JOIN_FLAG_CONTROL_PORT_HOST` 已备好）
   再看 —— 这条本来下一刀也要置，提前置的代价只是"固件开始等主机 EAPOL"；
4. 都不行才怀疑 vif：把 `AicScanVifEnsure` 换成"先 `MM_ADD_IF` 再立刻 join"（不用缓存的旧 vif）。

### 风险 2：`CFM` 成功但 `IND` 永远不来 —— 空口没回，或 IND 被我们漏掉

**为什么可能**：① AP 在别的信道/静默，auth 帧发出去没人应；② 固件把 IND 拆成了
`CFG_DATA_CFM(0x12)` 而不是 `CFG_CMD_RSP(0x11)`（我们的 `AicFrameCheck` 两者都收，应该没问题）；
③ IND 的 `plen` 被截断，`AicReceiveFrameTimeout` 按 852 读但固件只发到 IE 头（这时
`plen < 826`，会打 `(params cut before center_freq)`）；④ 3 s 预算不够（固件重传）。

**下一步**：
1. 看 `WiFi: join: ignore rx id=....` 行 —— 如果 IND 到了但 id 不是 0x1802（比如
   `SM_DISCONNECT_IND=0x1805` 先到），直接说明"关联被踢"，把那个 id/plen 记下来；
2. 如果没有 0x1802、但有大量 `0x1004`/`0x004f`，说明 RX 队列没排空、IND 被挤掉：
   把 `AIC_JOIN_IND_TRIES` 提到 8（4 s），并在 `JoinWaitId` 里对 `0x1805` 单独打印 reason_code；
3. 如果连 `ignore` 行都没有，是空口问题：确认扫描时该 AP 的 `rssi` 足够（<-75 的先换一个），
   并在 `JoinRun` 前后各打一次 `SdhciWifiReady()`/`AicWiFiVendorPrepare` 的结果，排除 SDIO 状态；
4. 真到这一步就该上"数据帧 RX 分支"（§7.1）了：即使 IND 丢了，只要 AP 发 M1，
   dump 数据帧也能证明关联其实成功了 —— 这也是下一刀的自然入口。
