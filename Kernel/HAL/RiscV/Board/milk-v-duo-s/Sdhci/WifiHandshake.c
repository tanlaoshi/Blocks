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

/* 收工：摘钩子 + 把 M1/M3/ANonce/PTK/KCK 这些"用完就不该留在 RAM 里"的东西清零 */
static void HandshakeStop(void) {
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

/*
 * 开控制端口那一段的退出判据：0x1405 到了就停（M1 是否顺带收到由 sAicM1Captured 记着，
 * 不在这个判据里 —— 收不到 CFM 才是硬失败）。
 */
static int HandshakeControlPortDone(void) {
    return (int)gAicEapolControlPortCfm;
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
        SdhciMark("WiFi: (control port is open, so either the AP never sent M1 or it is not"
                  " WPA2-PSK; a beacon-only stream means the control port did not open)\n");
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

const AIC_HANDSHAKE_M3 *AicHandshakeM3(void) {
    return &sAicM3;
}

/* 派生 → 组帧 → 发。任何一步失败都只打一行"卡在哪"，不伪造成功 */
static int HandshakeSendM2(SDHCI_HOST *Host, const AIC_SCAN_AP *Target, const char *Password,
                           UINT8 VifIndex, UINT8 ApIndex) {
    UINT8 Pmk[WIFI_PMK_LENGTH];
    UINT8 Snonce[WIFI_NONCE_LENGTH];
    UINT8 Ptk[WIFI_PTK_LENGTH];
    UINT8 Mic[WIFI_EAPOL_MIC_LENGTH];
    UINT8 Frame[AIC_HANDSHAKE_EAPOL_MAX];
    UINT32 Length = 0;
    int Status;

    Status = AicHandshakeDerive(Target, Password, &sAicM1, Pmk, Snonce, Ptk);
    if (Status == 0) {
        /* PTK 的前 16 B 就是 KCK（WifiCrypto/WifiPrf.h 的布局：KCK|KEK|TK） */
        AicHandshakeRememberKck(Ptk, sAicM1.KeyVersion);
        AicHandshakeRememberPtk(Ptk);
        Length = AicHandshakeM2Build(Frame, &sAicM1, Snonce, Ptk, Mic);
        SdhciMark("WiFi: eapol tx m2 len=");
        SdhciMarkDecimal("", Length);
        SdhciMark(" replay=");
        AicEapolMarkBytes("", sAicM1.Replay, AIC_EAPOL_REPLAY_LENGTH);
        SdhciMark(" mic=");
        AicEapolMarkBytes("", Mic, WIFI_EAPOL_MIC_LENGTH);
        SdhciMark("\n");
        Status = AicEapolSend(Host, Frame, Length, Target->Bssid, sAicM1.Station, VifIndex,
                              ApIndex);
    }
    if (Status != 0 && Length != 0u) {
        /* 发不出去时把帧原样打出来：这段二进制的对拍对象是空口，不是我们的日志 */
        SdhciMark("WiFi: eapol tx m2 hex[");
        SdhciMarkDecimal("", Length);
        SdhciMark("]");
        AicEapolMarkBytes(" ", Frame, Length);
        SdhciMark("\n");
        SdhciMark("WiFi: handshake FAILED at M2 tx: data TX refused (see the tx line above)\n");
        SdhciMark("WiFi: next = compare txdesc bytes with vendor rwnx_tx.c:1497-1530 +"
                  " aicwf_sdio.c:1530-1550 (ethtype byte order, staid/vif_idx, hwq);"
                  " then check whether the firmware wants hostid != 0 for a tx confirm\n");
    }
    /* 无论成功失败都把密钥材料清掉（PBKDF2 的 PMK、PTK、SNonce、M2 都在这些栈缓冲里） */
    memset(Pmk, 0, sizeof(Pmk));
    memset(Ptk, 0, sizeof(Ptk));
    memset(Snonce, 0, sizeof(Snonce));
    memset(Frame, 0, sizeof(Frame));
    return Status;
}

static int HandshakeWaitM3(SDHCI_HOST *Host) {
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

/*
 * 开控制端口并等 CFM(0x1405)，**同时**收数据帧：端口一开固件可能先吐排队已久的 M1、
 * 后写 CFM，两条都在同一条 SDIO FIFO 上。用同一条轮询把它们一起收，才不会把 M1
 * 当成"陌生配置帧"丢掉（早先想用 AicRecvMessageTimeout 单独等 CFM，那会把中间的
 * 数据帧直接判死）。
 */
static int HandshakeOpenControlPort(SDHCI_HOST *Host, UINT8 VifIndex, UINT8 ApIndex) {
    AIC_EAPOL_COUNTERS Counters;

    if (AicControlPortSend(Host, ApIndex) != 0) {
        return -1;
    }
    gAicEapolControlPortCfm = 0u;
    memset(&sAicM1, 0, sizeof(sAicM1));
    sAicM1Captured = 0u;
    sAicStage = AIC_HANDSHAKE_STAGE_M1;
    AicEapolSetKeySink(HandshakeKeySink);
    memset(&Counters, 0, sizeof(Counters));
    if (AicEapolPollRun(Host, AIC_HANDSHAKE_CONTROL_PORT_MS, &Counters,
                        HandshakeControlPortDone) != 0) {
        SdhciMark("WiFi: handshake FAILED at control port: SDIO read failed\n");
        return -1;
    }
    if (gAicEapolControlPortCfm == 0u) {
        SdhciMark("WiFi: handshake FAILED at control port: no ME_SET_CONTROL_PORT_CFM(0x1405)"
                  " in ");
        SdhciMarkDecimal("", AIC_HANDSHAKE_CONTROL_PORT_MS);
        SdhciMark("ms (firmware did not accept the control port request;"
                  " check sta_idx/ap_idx and the join flags)\n");
        return -1;
    }
    SdhciMark("WiFi: control port open (vif=");
    SdhciMarkDecimal("", VifIndex);
    SdhciMark(" ap=");
    SdhciMarkDecimal("", ApIndex);
    SdhciMark(")\n");
    return 0;
}

/* 密码给了才走这里：M1 → PTK/M2 → M3 → 装 PTK/GTK → M4 → 看数据帧 */
static int HandshakeRunKeyed(SDHCI_HOST *Host, const AIC_SCAN_AP *Target, const char *Password,
                             UINT8 VifIndex, UINT8 ApIndex) {
    if (AicHandshakeReceiveM1(Host) != 0) {
        return -1;
    }
    if (HandshakeSendM2(Host, Target, Password, VifIndex, ApIndex) != 0) {
        return -1;
    }
    if (HandshakeWaitM3(Host) != 0) {
        return -1;
    }
    /*
     * M3 的 MIC 已过：摘掉 sink。装钥匙期间 AP 可能重传 M3（它还没收到 M4），
     * 那些重传照旧由 WifiEapolKey.c 打印，但不该再改写上面留下的 M3 状态。
     */
    AicEapolSetKeySink(0);
    if (AicHandshakeInstallKeys(Host, Target, VifIndex, ApIndex) != 0) {
        return -1;
    }
    SdhciMark("WiFi: eapol handshake ok (keys installed: ptk+gtk)\n");
    AicHandshakeWatchData(Host);
    return 0;
}

int AicWifiHandshakeRun(SDHCI_HOST *Host, const AIC_SCAN_AP *Target, const char *Password,
                        UINT8 VifIndex, UINT8 ApIndex) {
    int Status;

    if (AicEapolLayoutCheck() != 0) {
        return -1;
    }
    /* 新一轮 join：钥匙这件事从"未知"重新算（上一轮装过与否不该影响本轮文案） */
    gAicEapolKeysInstalled = 0u;
    if (HandshakeOpenControlPort(Host, VifIndex, ApIndex) != 0) {
        HandshakeStop();
        return -1;
    }
    if (Password == 0 || Password[0] == 0) {
        HandshakeStop();
        SdhciMark("WiFi: join: no password, stop at control port"
                  " (add the password to run the 4-way handshake)\n");
        return 0;
    }
    Status = HandshakeRunKeyed(Host, Target, Password, VifIndex, ApIndex);
    HandshakeStop();
    return Status;
}
