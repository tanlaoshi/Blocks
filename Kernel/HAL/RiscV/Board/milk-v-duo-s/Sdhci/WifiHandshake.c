/*
 * WifiHandshake.c — PR-34/35：join 之后的四次握手顺序（控制端口 → M1 → PTK → M2 → M3
 *                   → 装 PTK/GTK → M4 → 看数据帧）
 *
 * 用户眼里：`wififw join Apple <密码>` 之后的一整段 —— `control port open`、
 *           `eapol rx … msg=M1`、`pbkdf2 N ms`、`ptk: …`、`eapol tx m2 …`、
 *           `eapol rx … msg=M3`、`key add: ptk/gtk …`、`eapol tx m4 …`，
 *           最后一行 `eapol handshake ok (keys installed: ptk+gtk)`。
 * 从哪读：AicWifiHandshakeRun（顺序）→ AicHandshakeReceiveM1（复用 EAPOL 轮询）
 *         → HandshakeSendM2（派生 + 组帧 + 发）→ HandshakeWaitM3（验 MIC）
 *         → AicHandshakeInstallKeys（WifiHandshakeInstall.c：装钥匙 + M4）
 *         → AicHandshakeWatchData（装完钥匙看解密帧）。
 * 别改：收包一律走 WifiEapol.c 的 AicEapolPollRun（本文件不另写 SDIO 收包）；
 *       M1/M3 的字段偏移来自 WifiCrypto 与 WifiEapolPrivate.h，这里不复制第二份。
 * 为什么用 sink 而不是改收包函数：M1 是"AP 什么时候发就什么时候到"的数据帧，
 *       把它塞进返回值会逼着改已验证的解析路径；挂一个钩子，解析照旧、打印照旧，
 *       握手只是"顺手接住"。
 */
#include "WifiHandshakePrivate.h"
#include "CString.h"

/* 握手阶段：sink 靠它区分"这帧是 M1 还是 M3"（同一个钩子，两个阶段） */
#define AIC_HANDSHAKE_STAGE_IDLE 0u
#define AIC_HANDSHAKE_STAGE_M1 1u
#define AIC_HANDSHAKE_STAGE_M3 2u
/* addr1 的 bit0 = 组播/广播位（802.11-2016 §9.2.4.1.2）；单播才能当 STA MAC 用 */
#define AIC_HANDSHAKE_MAC_MULTICAST 0x01u

static AIC_HANDSHAKE_M1 sAicM1;
/* M3 与 PTK 要活到"装钥匙"那一步：GTK 从 M3 的 Key Data 解，TK/KEK 在 PTK 里 */
static AIC_HANDSHAKE_M3 sAicM3;
static UINT8 sAicPtk[WIFI_PTK_LENGTH];
static UINT32 sAicStage = AIC_HANDSHAKE_STAGE_IDLE;
static UINT32 sAicM1Captured;
static UINT32 sAicM3Ok;
static UINT32 sAicM3BadPrinted;
static UINT8 sAicKck[WIFI_EAPOL_KCK_LENGTH];
static UINT8 sAicKeyVersion;
/*
 * AP 眼里的本机 STA MAC（M1 的 addr1）：DHCP 的 chaddr/源 MAC、ARP 的 sha 都要用它。
 * 这里**不随 HandshakeStop 清零**：握手收工时 M1 的副本必须抹掉（里面有 ANonce），
 * 但"这条关联用哪个 MAC"在断开之前一直是事实，`wififw dhcp` 在 join 之后才执行（PR-36）。
 */
static UINT8 sAicStaAddress[WIFI_MAC_ADDRESS_LENGTH];
static UINT32 sAicStaAddressValid;

/*
 * Key Descriptor Version（802.11-2016 §12.7.2 图 12-25 的 Key Information bit0-2）：
 * 2 = HMAC-SHA1（WPA2-PSK 的常态）、3 = AES-CMAC。别的值说明 AP 送来的不是我们支持的
 * 那一套，按 2 处理（宁可算出"MIC 不对"，也不静默当成 3 去算 CMAC）。
 */
static UINT8 HandshakeKeyVersion(UINT16 KeyInformation) {
    UINT32 Version = (UINT32)KeyInformation & AIC_EAPOL_KEY_INFORMATION_VERSION_MASK;

    if (Version == (UINT32)AIC_HANDSHAKE_VERSION_CMAC) {
        return (UINT8)AIC_HANDSHAKE_VERSION_CMAC;
    }
    return (UINT8)AIC_HANDSHAKE_VERSION_SHA1;
}

/*
 * 收工：摘钩子 + 把 M1/M3/ANonce/PTK/KCK 这些"用完就不该留在 RAM 里"的东西清零。
 * sAicStaAddress 例外（见上面的定义）：它不是密钥材料，是 DHCP/ARP 要用的本机地址。
 */
static void HandshakeKeySink(const UINT8 *Body, UINT32 Length, const UINT8 *Station);

void AicHandshakeSessionStop(void) {
    AicEapolSetKeySink(0);
    sAicStage = AIC_HANDSHAKE_STAGE_IDLE;
    sAicM1Captured = 0u;
    sAicM3Ok = 0u;
    sAicM3BadPrinted = 0u;
    memset(&sAicM1, 0, sizeof(sAicM1));
    memset(&sAicM3, 0, sizeof(sAicM3));
    memset(sAicPtk, 0, sizeof(sAicPtk));
    memset(sAicKck, 0, sizeof(sAicKck));
    sAicKeyVersion = 0u;
}

void AicHandshakeArmForM1(void) {
    memset(&sAicM1, 0, sizeof(sAicM1));
    sAicM1Captured = 0u;
    sAicStage = AIC_HANDSHAKE_STAGE_M1;
    AicEapolSetKeySink(HandshakeKeySink);
}

/* 轮询的提前退出判据：这一阶段要的东西到手就停，不等满预算 */
static int HandshakeStageDone(void) {
    if (sAicStage == AIC_HANDSHAKE_STAGE_M1) {
        return (int)sAicM1Captured;
    }
    if (sAicStage == AIC_HANDSHAKE_STAGE_M3) {
        return (int)sAicM3Ok;
    }
    return 0;
}

/* M1 = ACK 置位、MIC 不置位（802.11-2016 §12.7.6.2 的 Msg 1） */
static int HandshakeIsM1(UINT16 KeyInformation) {
    return (KeyInformation & AIC_EAPOL_KEY_INFORMATION_ACK) != 0u &&
           (KeyInformation & AIC_EAPOL_KEY_INFORMATION_MIC) == 0u;
}

/*
 * 接住 M1。**STA MAC 必须来自 addr1**：PTK 的 PRF 输入两边都算 (STA MAC, BSSID)，
 * AP 用的 STA MAC 就是它发 M1 的目的地址。用别的值 → PTK 不同 → 我们发的 M2
 * 在 AP 那里 MIC 校验失败，现象是"ACK 但不回 M3"，很难从别处看出来。
 */
static void HandshakeTakeM1(const UINT8 *Body, UINT32 Length, const UINT8 *Station,
                            UINT16 KeyInformation) {
    if (sAicM1Captured != 0u) {
        return; /* M1 重传：ANonce 相同，用第一份就够 */
    }
    if (Length > AIC_HANDSHAKE_EAPOL_MAX) {
        SdhciMarkHex("WiFi: handshake: M1 longer than budget, len=", Length, 4);
        SdhciMark("\n");
        return;
    }
    if (Station == 0 || (Station[0] & AIC_HANDSHAKE_MAC_MULTICAST) != 0u) {
        SdhciMark("WiFi: handshake: M1 has no usable addr1 (broadcast/unknown),"
                  " cannot pick the STA MAC for the PTK\n");
        return;
    }
    sAicM1.Length = Length;
    (void)memcpy(sAicM1.Frame, Body, Length);
    (void)memcpy(sAicM1.Anonce, Body + AIC_EAPOL_NONCE_OFFSET, AIC_EAPOL_NONCE_LENGTH);
    (void)memcpy(sAicM1.Replay, Body + AIC_EAPOL_REPLAY_OFFSET, AIC_EAPOL_REPLAY_LENGTH);
    (void)memcpy(sAicM1.Station, Station, WIFI_MAC_ADDRESS_LENGTH);
    /* 同一份地址留给 join 之后的 DHCP/ARP（见 sAicStaAddress 的说明） */
    (void)memcpy(sAicStaAddress, Station, WIFI_MAC_ADDRESS_LENGTH);
    sAicStaAddressValid = 1u;
    sAicM1.KeyInformation = KeyInformation;
    sAicM1.KeyVersion = HandshakeKeyVersion(KeyInformation);
    sAicM1Captured = 1u;
}

/*
 * M3 收下：验过 MIC 之后整帧留存 —— 装 GTK 要从 Key Data 解 KDE，回 M4 要抄 replay。
 * 存的是**EAPOL 帧长**（头里 length + 4），不是"可用字节数"：AP 的帧可能塞在更长的
 * 缓冲里，多出来的尾部字节会让 Key Data Length 的边界检查读到帧外。
 */
static void HandshakeKeepM3(const UINT8 *Body, UINT32 Length, UINT16 KeyInformation) {
    UINT32 Total = (UINT32)AicEapolGetBigEndian16(Body + 2) + AIC_EAPOL_HEADER_LENGTH;

    if (Total < AIC_EAPOL_KEY_FIXED_LENGTH || Total > Length ||
        Total > AIC_HANDSHAKE_EAPOL_MAX) {
        return;
    }
    memset(&sAicM3, 0, sizeof(sAicM3));
    (void)memcpy(sAicM3.Frame, Body, Total);
    sAicM3.Length = Total;
    (void)memcpy(sAicM3.Replay, Body + AIC_EAPOL_REPLAY_OFFSET, AIC_EAPOL_REPLAY_LENGTH);
    sAicM3.KeyInformation = KeyInformation;
    sAicM3.KeyVersion = sAicKeyVersion;
}

/* M3 = 带 MIC 的 AP 消息；MIC 对不对就是"AP 认不认可我们的 M2"的判决 */
static void HandshakeTakeM3(const UINT8 *Body, UINT32 Length, UINT16 KeyInformation) {
    if ((KeyInformation & AIC_EAPOL_KEY_INFORMATION_MIC) == 0u) {
        return; /* M1 重传：没有 MIC，不是 M3 */
    }
    if (Length < AIC_EAPOL_KEY_FIXED_LENGTH) {
        return;
    }
    if (AicHandshakeMicCheck(Body, Length, sAicKeyVersion, sAicKck) != 0) {
        sAicM3Ok = 1u;
        HandshakeKeepM3(Body, Length, KeyInformation);
        SdhciMark("WiFi: eapol rx: M3 mic ok (AP accepted our M2; PTK matches)\n");
        return;
    }
    if (sAicM3BadPrinted == 0u) {
        sAicM3BadPrinted = 1u;
        SdhciMark("WiFi: eapol rx: M3 mic BAD (AP got our M2 but the PTK differs: wrong password,"
                  " or the STA MAC/SNonce used in the PTK is not what went on air)\n");
    }
}

/* WifiEapolKey.c 每解出一帧 EAPOL-Key 就调这里（打印之后） */
static void HandshakeKeySink(const UINT8 *Body, UINT32 Length, const UINT8 *Station) {
    UINT16 KeyInformation;

    if (Length < AIC_EAPOL_KEY_FIXED_LENGTH) {
        return;
    }
    KeyInformation = AicEapolGetBigEndian16(Body + AIC_EAPOL_KEY_INFORMATION_OFFSET);
    if (sAicStage == AIC_HANDSHAKE_STAGE_M1 && HandshakeIsM1(KeyInformation) != 0) {
        HandshakeTakeM1(Body, Length, Station, KeyInformation);
    } else if (sAicStage == AIC_HANDSHAKE_STAGE_M3) {
        HandshakeTakeM3(Body, Length, KeyInformation);
    }
}

int AicHandshakeReceiveM1(SDHCI_HOST *Host) {
    AIC_EAPOL_COUNTERS Counters;

    /* 控制端口那段可能已经把 M1 收进来了（见 HandshakeOpenControlPort）：有就不再等 */
    if (sAicM1Captured == 0u) {
        memset(&sAicM1, 0, sizeof(sAicM1));
        sAicStage = AIC_HANDSHAKE_STAGE_M1;
        AicEapolSetKeySink(HandshakeKeySink);
        memset(&Counters, 0, sizeof(Counters));
        SdhciMark("WiFi: handshake: watching data frames for M1 (budget=");
        SdhciMarkDecimal("", AIC_HANDSHAKE_M1_MS);
        SdhciMark("ms)\n");
        if (AicEapolPollRun(Host, AIC_HANDSHAKE_M1_MS, &Counters, HandshakeStageDone) != 0) {
            SdhciMark("WiFi: handshake FAILED at M1: SDIO read failed (FIFO may be out of sync;"
                      " re-run join)\n");
            return -1;
        }
    }
    if (sAicM1Captured == 0u) {
        SdhciMark("WiFi: handshake FAILED at M1: no EAPOL-Key M1 in ");
        SdhciMarkDecimal("", AIC_HANDSHAKE_M1_MS);
        SdhciMark("ms\n");
        SdhciMark("WiFi: (M1 missing: AP silent, not WPA2-PSK, or CONTROL_PORT_HOST/join flags;"
                  " early ME_SET_CONTROL_PORT is not required for M1 — see WifiControlPort.c)\n");
        return -1;
    }
    return 0;
}

void AicHandshakeRememberKck(const UINT8 *Kck, UINT8 KeyVersion) {
    (void)memcpy(sAicKck, Kck, WIFI_EAPOL_KCK_LENGTH);
    sAicKeyVersion = KeyVersion;
}

/*
 * PTK 也留一份：装钥匙要用 TK（PTK 偏移 32）和 KEK（偏移 16，解 GTK），
 * 而 M2 的栈缓冲在 HandshakeSendM2 里就清零了。HandshakeStop 统一清掉。
 */
void AicHandshakeRememberPtk(const UINT8 *Ptk) {
    (void)memcpy(sAicPtk, Ptk, WIFI_PTK_LENGTH);
}

const UINT8 *AicHandshakeKck(void) {
    return sAicKck;
}

const UINT8 *AicHandshakePtk(void) {
    return sAicPtk;
}

const UINT8 *AicHandshakeStation(void) {
    return sAicM1.Station;
}

/*
 * 关联期间的空口 STA MAC（M1 的 addr1）。HandshakeStop 之后 AicHandshakeStation()
 * 指向的 M1 副本已经被清零，DHCP/ARP 要用这里；没握过手返回 0。
 */
const UINT8 *AicHandshakeStationAddress(void) {
    return sAicStaAddressValid != 0u ? sAicStaAddress : 0;
}

const AIC_HANDSHAKE_M1 *AicHandshakeM1(void) {
    return &sAicM1;
}

const AIC_HANDSHAKE_M3 *AicHandshakeM3(void) {
    return &sAicM3;
}

int AicHandshakeWaitM3(SDHCI_HOST *Host) {
    AIC_EAPOL_COUNTERS Counters;

    sAicM3Ok = 0u;
    sAicM3BadPrinted = 0u;
    sAicStage = AIC_HANDSHAKE_STAGE_M3;
    memset(&Counters, 0, sizeof(Counters));
    if (AicEapolPollRun(Host, AIC_HANDSHAKE_M3_MS, &Counters, HandshakeStageDone) != 0) {
        SdhciMark("WiFi: handshake FAILED at M3: SDIO read failed (FIFO may be out of sync)\n");
        return -1;
    }
    if (sAicM3Ok == 0u) {
        SdhciMark("WiFi: handshake FAILED at M3: no M3 with a valid MIC in ");
        SdhciMarkDecimal("", AIC_HANDSHAKE_M3_MS);
        SdhciMark("ms (frames=");
        SdhciMarkDecimal("", Counters.Frames);
        SdhciMark(" key=");
        SdhciMarkDecimal("", Counters.KeyFrames);
        SdhciMark(")\n");
        return -1;
    }
    return 0;
}
