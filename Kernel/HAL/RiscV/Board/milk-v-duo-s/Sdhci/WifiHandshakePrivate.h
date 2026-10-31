/*
 * WifiHandshakePrivate.h — PR-34/35：四次握手（M1 → PTK → M2 → M3 → 装钥匙 → M4）的内部声明
 *
 * 用户眼里：join 之后那几行 —— `eapol rx … msg=M1`、`pbkdf2 N ms`、`ptk: …`、
 *           `eapol tx m2 …`、`eapol rx … msg=M3`、`key add: ptk/gtk …`、
 *           `eapol tx m4 len=99 …`，最后 `eapol handshake ok (keys installed: ptk+gtk)`。
 * 从哪读：AicWifiHandshakeRun（WifiHandshake.c，顺序）→ AicHandshakeDerive /
 *         AicHandshakeM2Build / AicHandshakeMicCheck / AicHandshakeM4Build
 *         （WifiHandshakeKey.c，算法与组帧）→ AicHandshakeInstallKeys /
 *         AicHandshakeWatchData（WifiHandshakeInstall.c，装钥匙 + M4 + 看数据帧）
 *         → AicKeyAddPairwise / AicKeyAddGroup（WifiKeyAdd.c，MM_KEY_ADD_REQ）
 *         → AicEapolSend（WifiEapolTx.c，数据 TX）。
 * 别改：EAPOL-Key 的字段偏移（MIC@81 / KeyDataLen@97 / KeyData@99）来自公共模块
 *       WifiCrypto/WifiEapol.h，本文件不另立一份；nonce/PTK 长度来自 WifiCrypto/WifiPrf.h；
 *       MM_KEY_ADD 的字段顺序/宽度来自厂商 lmac_msg.h，偏移由 AicKeyAddLayoutCheck
 *       在发送前核对（sizeof 不是手写数字，是编译出来的）。
 */
#ifndef SDHCI_WIFI_HANDSHAKE_PRIVATE_H
#define SDHCI_WIFI_HANDSHAKE_PRIVATE_H

#include "WifiEapolPrivate.h"
#include "WifiCrypto/WifiPbkdf2.h"
#include "WifiCrypto/WifiPrf.h"

/*
 * 收下来的 EAPOL-Key 帧上限：99 B 固定头 + Key Data。
 * M1 的 PMKID KDE 是 2+16=18 B（121 B 总长）、M3 的 GTK KDE 是 2+6+16=24 B（123 B），
 * 192 B 给 32 B GTK + 备选密钥都留够；超出就不收（宁可报错也不截断后算 MIC）。
 */
#define AIC_HANDSHAKE_EAPOL_MAX 192u
/*
 * 等 M1 / 等 M3 的预算：都按 EAPOL 轮询的默认档（3000 ms）。
 * 厂商/真机 M1 在 assoc 后几十 ms 内到；3 s 是给 AP 重传留的余量，不是协议常量。
 */
#define AIC_HANDSHAKE_M1_MS AIC_EAPOL_POLL_DEFAULT_MS
/*
 * 等 ME_SET_CONTROL_PORT_CFM(0x1405) 的预算：与 join 的 CFM 同一档（4 × 500 ms）。
 * 这一段同时收数据帧（M1 可能比 CFM 先到），所以不是纯等待。
 */
#define AIC_HANDSHAKE_CONTROL_PORT_MS 2000u
/* M3 要等 AP 收完 M2 再发，同一档就够（AP 也会重传 M3） */
#define AIC_HANDSHAKE_M3_MS AIC_EAPOL_POLL_DEFAULT_MS
/*
 * 装完钥匙后看"解密成功的数据帧"的预算：3 s。现场要求 2~3 s；AP 的广播/组播
 * （ARP/DHCP 之类）通常 1 s 内就有，3 s 是给空闲 AP 留的余量，不是协议常量。
 */
#define AIC_HANDSHAKE_DATA_MS 3000u
/*
 * 等 MM_KEY_ADD_CFM(0x25) 的预算：与 join 的 CFM 同一档（4 × AIC_CFM_TIMEOUT_MS = 2000 ms）。
 * 这一段走 EAPOL 轮询（数据帧照常解析），所以不是纯等待。
 */
#define AIC_HANDSHAKE_KEY_ADD_MS 2000u
/* 802.11-2016 §12.7.2 图 12-25：M2 的 Key Information = pairwise|MIC|版本 */
#define AIC_HANDSHAKE_M2_FLAGS 0x0108u /* AIC_EAPOL_KEY_INFORMATION_MIC|_PAIRWISE */
/*
 * M4 的 Key Information（802.11-2016 §12.7.6.2 的 Msg 4）：
 *   pairwise | install | MIC | secure，ack=0，版本位另加（沿用 M3 的 Key Descriptor Version）。
 * 对拍对象是 wpa_supplicant 2.10（wpa.c:1634-1640）：
 *   key_info &= SECURE; key_info |= ver | PAIRWISE; key_info |= MIC;
 * 它**不置 install**（真机抓包常见 0x030a）；本刀按现场要求置 install（0x034a），
 * 也就是现场判据行里逐位写死的那一组。AP 在 M4 里只看 MIC/replay/secure，
 * install 位不改变它的状态机；两个值在实际部署里都能完成握手。
 */
#define AIC_HANDSHAKE_M4_FLAGS                              \
    (AIC_EAPOL_KEY_INFORMATION_PAIRWISE |                   \
     AIC_EAPOL_KEY_INFORMATION_INSTALL |                    \
     AIC_EAPOL_KEY_INFORMATION_MIC |                        \
     AIC_EAPOL_KEY_INFORMATION_SECURE)
/*
 * M4 的 Key Length 字段：RSN 写 0（wpa_supplicant 2.10 wpa.c:1641-1642
 * `WPA_PUT_BE16(reply->key_length, 0)`）；WPA（非 RSN）才抄 M3 的值。
 * M4 没有 Key Data（KeyDataLen = 0）⇒ 整帧就是 99 B 固定头。
 */
#define AIC_HANDSHAKE_M4_KEY_LENGTH 0u
/* Key Length 字段：CCMP 固定 16 B（802.11-2016 §12.7.2 图 12-25 的 Key Length） */
#define AIC_HANDSHAKE_KEY_LENGTH 16u
/* M2 Key Data 里带 22 B RSN IE（与 assoc 请求同一份，见 WifiAssocMessage.c） */
#define AIC_HANDSHAKE_KEY_DATA_LENGTH AIC_JOIN_RSN_IE_LENGTH
/* Key Descriptor Version 只有低 3 位有效（802.11-2016 §12.7.2）；2 = HMAC-SHA1 */
#define AIC_HANDSHAKE_VERSION_SHA1 2u
#define AIC_HANDSHAKE_VERSION_CMAC 3u
/* PBKDF2 超过这个数就提示（用户要的判据：RISC-V 上的实测耗时） */
#define AIC_HANDSHAKE_PBKDF2_WARN_MS 500u
/* SNonce 的熵材料：两次 rdtime（各 8 B）+ bssid 6 + rssi 1 + channel 1 + 计数器 4 = 28 */
#define AIC_HANDSHAKE_SEED_LENGTH 28u
/* SHA-1 摘要 20 B，两个域拼起来 40 B ≥ 32 B 的 nonce */
#define AIC_HANDSHAKE_SNONCE_ROUNDS 2u
#define AIC_HANDSHAKE_SNONCE_LABEL "AIC8800-SNONCE"
#define AIC_HANDSHAKE_SNONCE_LABEL_LENGTH 14u

/*
 * 一次握手里的 M1：整个 EAPOL 帧原样留着（M2 的 EAPOL 版本、描述符类型、
 * 重放计数器都从它抄），外加从 802.11 头 addr1 取到的本机 STA MAC。
 * **STA MAC 必须来自 M1 的 addr1**：PTK 的 PRF 输入两边都算 (STA, BSSID)，
 * 而 AP 用的 STA MAC 就是它自己发 M1 时的目的地址；用别的值 AP 算出来的 PTK 就不一样。
 */
typedef struct {
    UINT8 Frame[AIC_HANDSHAKE_EAPOL_MAX];   /* EAPOL 帧（从 version 字节开始） */
    UINT32 Length;                          /* 实际收到的 EAPOL 帧长 */
    UINT8 Station[WIFI_MAC_ADDRESS_LENGTH]; /* M1 的 addr1 = 本机 STA MAC */
    UINT8 Anonce[WIFI_NONCE_LENGTH];        /* EAPOL +17 */
    UINT8 Replay[AIC_EAPOL_REPLAY_LENGTH];  /* EAPOL +9 */
    UINT16 KeyInformation;                  /* EAPOL +5 */
    UINT8 KeyVersion;                       /* KeyInformation & 7，非 3 时按 2 走 */
} AIC_HANDSHAKE_M1;

/*
 * 一次握手里的 M3：装 GTK 要从它的 Key Data 里解 KDE，发 M4 要抄它的 replay/版本，
 * 所以验过 MIC 之后整帧留到装完钥匙为止（HandshakeStop 清掉）。
 *   KeyInformation：EAPOL +5（encrypted 位决定 Key Data 是不是 RFC 3394 包过的）
 *   Replay：EAPOL +9（M4 的 replay 必须与 M3 相同，802.11-2016 §12.7.6.2）
 */
typedef struct {
    UINT8 Frame[AIC_HANDSHAKE_EAPOL_MAX];  /* EAPOL 帧（从 version 字节开始，按 length 截） */
    UINT32 Length;                         /* EAPOL 头里 length + 4（不是可用字节数） */
    UINT8 Replay[AIC_EAPOL_REPLAY_LENGTH]; /* EAPOL +9 */
    UINT16 KeyInformation;                 /* EAPOL +5 */
    UINT8 KeyVersion;                      /* KeyInformation & 7，非 3 时按 2 走 */
} AIC_HANDSHAKE_M3;

/* ---- WifiHandshake.c：顺序编排 ---- */
/* 收 M1（内部用 EAPOL 轮询；武装之后 AP 的 M1 一到就停）。收下的东西留在模块内，
   由 HandshakeSendM2 直接读（ANonce/STA MAC/重放计数都只在这一处，不复制第二份）。0 = 收齐 */
int AicHandshakeReceiveM1(SDHCI_HOST *Host);
/* KCK 暂存（M3 验 MIC 要用；用完/失败都清零） */
void AicHandshakeRememberKck(const UINT8 *Kck, UINT8 KeyVersion);
/* PTK 暂存（M4 之后才装钥匙：TK 给 PTK、KEK 解 GTK；HandshakeStop 清零） */
void AicHandshakeRememberPtk(const UINT8 *Ptk);
/* 装钥匙那一段只读不写：KCK（M4 的 MIC）、PTK（TK/KEK）、M3 原帧（replay/Key Data）、
   本机 STA MAC（M4 的源地址必须与 PTK 里用的一致）。返回的指针在 HandshakeStop 前有效 */
const UINT8 *AicHandshakeKck(void);
const UINT8 *AicHandshakePtk(void);
const UINT8 *AicHandshakeStation(void);
/* 关联期间的空口 STA MAC（M1 的 addr1）；HandshakeStop 之后仍有效，没握过手返回 0。
   DHCP 的 chaddr/源 MAC 与 ARP 的 sha 用它（PR-36） */
const UINT8 *AicHandshakeStationAddress(void);
const AIC_HANDSHAKE_M3 *AicHandshakeM3(void);

/* ---- WifiHandshakeKey.c：PMK / SNonce / PTK / M2 / MIC ---- */
/* 0 = 成功；失败时已打印原因。Pmk/Snonce/Ptk 由调用方清零 */
int AicHandshakeDerive(const AIC_SCAN_AP *Target, const char *Password,
                       const AIC_HANDSHAKE_M1 *M1, UINT8 Pmk[WIFI_PMK_LENGTH],
                       UINT8 Snonce[WIFI_NONCE_LENGTH], UINT8 Ptk[WIFI_PTK_LENGTH]);
/* 返回 M2 的 EAPOL 帧长；Mic 是算出来的 MIC（帧里也已写好） */
UINT32 AicHandshakeM2Build(UINT8 *Frame, const AIC_HANDSHAKE_M1 *M1, const UINT8 *Snonce,
                           const UINT8 *Kck, UINT8 Mic[WIFI_EAPOL_MIC_LENGTH]);
/* 1 = 收到的帧 MIC 与 KCK 算出来的一致（AP 与我们的 PTK 相同） */
int AicHandshakeMicCheck(const UINT8 *Body, UINT32 Length, UINT8 KeyVersion, const UINT8 *Kck);
/* 返回 M4 的 EAPOL 帧长（99，没有 Key Data）；Mic 是算出来的 MIC（帧里也已写好） */
UINT32 AicHandshakeM4Build(UINT8 *Frame, const AIC_HANDSHAKE_M3 *M3, const UINT8 *Kck,
                           UINT8 Mic[WIFI_EAPOL_MIC_LENGTH]);
/* 打印 ptk/kck/kek/tk 各前 16 B（验收判据那一行） */
void AicHandshakePrintPtk(const UINT8 *Ptk);
void AicHandshakePrintSnonce(const UINT8 *Snonce);

/* ---- WifiEapolTx.c：数据 TX（本仓第一条非消息帧） ---- */
int AicEapolSend(SDHCI_HOST *Host, const UINT8 *Eapol, UINT32 Length, const UINT8 *Destination,
                 const UINT8 *Source, UINT8 VifIndex, UINT8 ApIndex);
/*
 * PR-36：同一条 SDIO 数据通道发**任意以太网帧**（DHCP/ARP 用）。
 * Frame = dst[6] src[6] ethertype[2] payload，Length 含 14 B 以太网头且至少 60 B；
 * 802.11 头与 LLC/SNAP 仍由固件补（与 EAPOL 完全同一条 txdesc 契约）。
 */
int AicWifiDataSend(SDHCI_HOST *Host, const UINT8 *Frame, UINT32 Length, UINT8 VifIndex,
                    UINT8 ApIndex);

/*
 * ---- MM_KEY_ADD_REQ(0x24) / CFM(0x25) 的镜像（PR-35）----
 * 结构（lmac_msg.h:797-812 + lmac_mac.h:292-297），厂商头实测：
 *   sizeof(mm_key_add_req) = 44、sizeof(mac_sec_key) = 36、sizeof(mm_key_add_cfm) = 2
 *   offsetof key_idx=0 sta_idx=1 key=4 key.length=4 key.array=8
 *            cipher_suite=40 inst_nbr=41 spp=42 pairwise=43
 * 复现：宿主 gcc + 最小 linux 桩 include 厂商 lmac_msg.h 打印 sizeof/offsetof
 *       （调研文档 §2 的 probe 方法；2026-10-31 重跑过一次，上面这些数字全部一致）。
 * 厂商填法（rwnx_send_key_add，rwnx_msg_tx.c:640-680 + rwnx_main.c:3677-3680）：
 *   pairwise：sta_idx = IND.ap_idx、key_idx = 0、inst_nbr = vif、pairwise = 1
 *   默认钥匙：sta_idx = 0xFF（`sta ? sta->sta_idx : 0xFF`）、key_idx = KeyID、pairwise = 0
 * 本刀的关键差别（真机空口实测，见 WifiAssocPrivate.h 的 RSN 说明）：
 *   这个 AP 是 WPA/WPA2 混合，**组播 TKIP、单播 CCMP** ⇒
 *   PTK 的 cipher_suite = MAC_CIPHER_CCMP(2)、GTK 的 = MAC_CIPHER_TKIP(1)；
 *   厂商日志里同样是 `cipher:2 key_len:16`（PTK）+ `cipher:1 key_len:32`（GTK）。
 */
#define AIC_KEY_SEC_KEY_LENGTH 32u /* MAC_SEC_KEY_LEN（lmac_mac.h:289） */
#define AIC_KEY_SEC_KEY_WORDS (AIC_KEY_SEC_KEY_LENGTH / 4u) /* u32_l array[8] */
#define AIC_KEY_ADD_LENGTH 44u     /* sizeof(struct mm_key_add_req)，编译期核对用 */
#define AIC_KEY_CFM_LENGTH 2u      /* sizeof(struct mm_key_add_cfm) */
/* enum mac_cipher_suite（lmac_mac.h:139-168） */
#define AIC_KEY_CIPHER_TKIP 1u /* MAC_CIPHER_TKIP：组播（GTK） */
#define AIC_KEY_CIPHER_CCMP 2u /* MAC_CIPHER_CCMP：单播（PTK） */
/* 默认钥匙的 sta_idx（rwnx_main.c:3678 `sta ? sta->sta_idx : 0xFF`） */
#define AIC_KEY_ADD_DEFAULT_STA_INDEX 0xFFu
/* pairwise 钥匙的 key_idx：cfg80211 对 PTK 给 0（lmac_msg.h:800 "valid only for default keys"） */
#define AIC_KEY_ADD_PAIRWISE_INDEX 0u
#define AIC_KEY_ADD_SPP_DISABLED 0u /* spp = A-MSDU SPP，厂商 zalloc 后不填 */
#define AIC_KEY_ADD_PAIRWISE 1u
#define AIC_KEY_ADD_NOT_PAIRWISE 0u

typedef struct {
    UINT8 Length;                        /* +0  key.length：16（PTK）/ 32（TKIP GTK） */
    UINT8 Pad[3];                        /* +1  对齐 u32_l array（厂商侧的编译器也插） */
    UINT32 Array[AIC_KEY_SEC_KEY_WORDS]; /* +4  钥匙材料，按字节拷（mac_sec_key.array） */
} AIC_MAC_SEC_KEY;                       /* sizeof = 36 */

typedef struct {
    UINT8 KeyIndex;       /* +0  key_idx：PTK 填 0，GTK 填 KDE 的 KeyID */
    UINT8 StaIndex;       /* +1  sta_idx：PTK 填 ap_idx，GTK 填 0xFF */
    UINT8 Pad[2];         /* +2  对齐 key.array 的 u32（厂商侧同样插） */
    AIC_MAC_SEC_KEY Key;  /* +4  36 B */
    UINT8 CipherSuite;    /* +40 MAC_CIPHER_TKIP(1) / MAC_CIPHER_CCMP(2) */
    UINT8 InstanceNumber; /* +41 inst_nbr = vif */
    UINT8 Spp;            /* +42  */
    UINT8 Pairwise;       /* +43 bool_l：1 = pairwise，0 = 默认钥匙 */
} AIC_MM_KEY_ADD;         /* sizeof = 44 */

/* struct mm_key_add_cfm：status（0 = 成功）+ hw_key_idx（固件分配） */
typedef struct {
    UINT8 Status;     /* +0 */
    UINT8 HwKeyIndex; /* +1 */
} AIC_MM_KEY_ADD_CONFIRM;

/*
 * 组播套件的"RSN 编号"（末字节）：00-0F-AC:2 = TKIP ⇒ 2。
 * 注意别和 MAC_CIPHER_TKIP(1) 混：WifiGtkExtract 的 GroupCipher 参数要的是 RSN 编号
 * （公共模块 WifiEapol.c 的 WIFI_GROUP_CIPHER_TKIP = 0x02），
 * MM_KEY_ADD 的 cipher_suite 要的是 MAC_CIPHER_* 编号 —— 两个名字分开写。
 */
#define AIC_HANDSHAKE_GROUP_CIPHER_TKIP 0x02u

/* ---- WifiKeyAdd.c：把钥匙交给固件（装 PTK / 装 GTK） ---- */
/* sizeof/offsetof 自检；排歪了就拒绝发送（同 AicEapolLayoutCheck 的风格） */
int AicKeyAddLayoutCheck(void);
/* 拼 44 B 请求；返回 sizeof（Length 越界返回 0）。Buffer 要 4 B 对齐 */
UINT32 AicKeyAddBuild(UINT8 *Buffer, UINT8 VifIndex, UINT8 StaIndex, UINT8 Pairwise,
                      const UINT8 *Key, UINT8 KeyLength, UINT8 KeyIndex, UINT8 CipherSuite);
/* PTK：sta_idx = ap_idx、key_idx = 0、cipher = CCMP、key = TK(16 B)、pairwise = 1。
   打印 `key add: ptk sta=… cipher=ccmp(2) rc=… cfm=…`；0 = 固件 CFM 说装好了 */
int AicKeyAddPairwise(SDHCI_HOST *Host, UINT8 VifIndex, UINT8 StaIndex, const UINT8 *Tk);
/* GTK：sta_idx = 0xFF、key_idx = KeyID、cipher = TKIP、key/长度按 KDE、pairwise = 0。
   打印 `key add: gtk sta=255 keyid=… cipher=tkip(1) rc=… cfm=…` */
int AicKeyAddGroup(SDHCI_HOST *Host, UINT8 VifIndex, const WIFI_GTK *Gtk);

/* ---- WifiHandshakeInstall.c：装钥匙 → 发 M4 → 看数据帧 ---- */
/*
 * M3 验过 MIC 之后的收尾：装 PTK → 从 M3 解 GTK 并装上 → 回 M4。
 * 0 = PTK 与 GTK 都装好且 M4 发出去了；失败时已打印卡在哪一步。
 */
int AicHandshakeReinstallGtk(SDHCI_HOST *Host, UINT8 VifIndex);
int AicHandshakeInstallKeys(SDHCI_HOST *Host, const AIC_SCAN_AP *Target, UINT8 VifIndex,
                            UINT8 ApIndex);
/*
 * 更强的判据：装完钥匙后再盯 AIC_HANDSHAKE_DATA_MS，把固件**解密成功**的数据帧
 * （hw_rxhdr.status 的 decr != 0）打出来。出现就说明数据通路已活（下一步 DHCP）。
 */
void AicHandshakeWatchData(SDHCI_HOST *Host);

#endif
