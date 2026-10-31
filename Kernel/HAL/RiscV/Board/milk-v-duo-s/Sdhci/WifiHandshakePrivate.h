/*
 * WifiHandshakePrivate.h — PR-34：四次握手（M1 → PTK → M2 → M3）的内部声明
 *
 * 用户眼里：join 之后那几行 —— `eapol rx … msg=M1`、`pbkdf2 N ms`、`ptk: …`、
 *           `eapol tx m2 …`、`eapol rx … msg=M3`。
 * 从哪读：AicWifiHandshakeRun（WifiHandshake.c，顺序）→ AicHandshakeDerive /
 *         AicHandshakeM2Build / AicHandshakeMicCheck（WifiHandshakeKey.c，算法与组帧）
 *         → AicEapolSend（WifiEapolTx.c，数据 TX）。
 * 别改：EAPOL-Key 的字段偏移（MIC@81 / KeyDataLen@97 / KeyData@99）来自公共模块
 *       WifiCrypto/WifiEapol.h，本文件不另立一份；nonce/PTK 长度来自 WifiCrypto/WifiPrf.h。
 * 不做：MM_KEY_ADD（装钥匙）—— M3 的 MIC 验过就够判"AP 认可了我们的 M2"，
 *       装钥匙是下一刀（sta_idx 已由 AicJoinLastIndexes 留下）。
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
/* 802.11-2016 §12.7.2 图 12-25：M2 的 Key Information = pairwise|MIC|版本 */
#define AIC_HANDSHAKE_M2_FLAGS 0x0108u /* AIC_EAPOL_KEY_INFORMATION_MIC|_PAIRWISE */
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

/* ---- WifiHandshake.c：顺序编排 ---- */
/* 收 M1（内部用 EAPOL 轮询；武装之后 AP 的 M1 一到就停）。收下的东西留在模块内，
   由 HandshakeSendM2 直接读（ANonce/STA MAC/重放计数都只在这一处，不复制第二份）。0 = 收齐 */
int AicHandshakeReceiveM1(SDHCI_HOST *Host);
/* KCK 暂存（M3 验 MIC 要用；用完/失败都清零） */
void AicHandshakeRememberKck(const UINT8 *Kck, UINT8 KeyVersion);

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
/* 打印 ptk/kck/kek/tk 各前 16 B（验收判据那一行） */
void AicHandshakePrintPtk(const UINT8 *Ptk);
void AicHandshakePrintSnonce(const UINT8 *Snonce);

/* ---- WifiEapolTx.c：数据 TX（本仓第一条非消息帧） ---- */
int AicEapolSend(SDHCI_HOST *Host, const UINT8 *Eapol, UINT32 Length, const UINT8 *Destination,
                 const UINT8 *Source, UINT8 VifIndex, UINT8 ApIndex);

#endif
