/*
 * WifiEapolKey.c — EAPOL-Key 帧的字段解码与打印（只读，不验 MIC）
 *
 * 用户眼里：`wififw eapol rx len=… key_info=0x… replay=… anonce=… mic=… msg=M1`
 *           这两行就是这里打的。
 * 从哪读：AicEapolParseBody（EAPOL 头分流：Key / 非 Key）
 *         → EapolPrintKeyFrame（99 B 固定头 + Key Data 前 24 B）
 *         → AicEapolDumpHead（第一帧数据帧的取证 dump）。
 * 别改：字段偏移是 802.11-2016 §12.7.2 图 12-25 的字节契约；MIC@81、KeyData@99
 *       直接用公共模块 WifiCrypto/WifiEapol.h 的宏，不另立一份（改一处即两边同步）。
 * 本文件只"读"：不验 MIC、不派生 PTK、不装钥匙。PR-34 加了一个 sink 钩子，把
 *       解出来的 Key 帧原样交给 WifiHandshake.c（谁来用、怎么用不在这里）。
 */
#include "WifiEapolPrivate.h"

/* 唯一的 Key 帧钩子（0 = 没人接）。定义在这里，声明在 WifiEapolPrivate.h */
AIC_EAPOL_KEY_SINK gAicEapolKeySink;

/* EAPOL 的多字节字段全是大端（802.1X-2020 §11.3）；Parse 与 Key 两个文件共用 */
UINT16 AicEapolGetBigEndian16(const UINT8 *At) {
    return (UINT16)(((UINT16)At[0] << 8) | (UINT16)At[1]);
}

/* Tag = "" 时是一串紧凑十六进制（nonce/mic）；取证 dump 传 " " 便于按字节读 */
void AicEapolMarkBytes(const char *Tag, const UINT8 *Data, UINT32 Length) {
    UINT32 Index;

    for (Index = 0; Index < Length; Index++) {
        SdhciMarkHex(Tag, (UINT32)Data[Index], 2);
    }
}

/*
 * 取证：第一帧数据帧的 +0..31（hw_rxhdr）与 +60..75（802.11 头）。
 * 调研文档 §7.1 明确要求"MPDU 偏移真机对一次，别照抄 60" —— 这两行就是用来对的。
 */
void AicEapolDumpHead(const UINT8 *Frame, UINT32 FrameLength) {
    UINT32 Head = FrameLength > AIC_EAPOL_HEAD_DUMP ? AIC_EAPOL_HEAD_DUMP : FrameLength;

    SdhciMark("WiFi: eapol data hdr[");
    SdhciMarkDecimal("", Head);
    SdhciMark("]");
    AicEapolMarkBytes(" ", Frame, Head);
    SdhciMark("\n");
    if (FrameLength > AIC_RX_HW_HEADER_LENGTH) {
        UINT32 Count = FrameLength - AIC_RX_HW_HEADER_LENGTH;

        if (Count > AIC_EAPOL_MPDU_DUMP) {
            Count = AIC_EAPOL_MPDU_DUMP;
        }
        SdhciMark("WiFi: eapol data mpdu[");
        SdhciMarkDecimal("", Count);
        SdhciMark("]@");
        SdhciMarkDecimal("", AIC_RX_HW_HEADER_LENGTH);
        AicEapolMarkBytes(" ", Frame + AIC_RX_HW_HEADER_LENGTH, Count);
        SdhciMark("\n");
    }
}

/* 四次握手的四步（802.11-2016 §12.7.6.2）：只看 ack/mic/install/secure 四位 */
static const char *EapolMessageName(UINT16 KeyInformation) {
    UINT32 Acknowledge = (KeyInformation & AIC_EAPOL_KEY_INFORMATION_ACK) != 0u;
    UINT32 Mic = (KeyInformation & AIC_EAPOL_KEY_INFORMATION_MIC) != 0u;
    UINT32 Install = (KeyInformation & AIC_EAPOL_KEY_INFORMATION_INSTALL) != 0u;
    UINT32 Secure = (KeyInformation & AIC_EAPOL_KEY_INFORMATION_SECURE) != 0u;

    if (Acknowledge != 0u && Mic == 0u && Install == 0u) {
        return "M1";
    }
    if (Acknowledge == 0u && Mic != 0u && Secure == 0u) {
        return "M2";
    }
    if (Acknowledge != 0u && Mic != 0u && Install != 0u) {
        return "M3";
    }
    if (Acknowledge == 0u && Mic != 0u && Secure != 0u) {
        return "M4";
    }
    return "other";
}

/*
 * 第一行按验收格式：len / key_info / replay / anonce / mic / msg。
 * 收到的 Key 帧全是 AP → STA，所以 +17 的 32 B 是 **ANonce**（M2/M4 才是 SNonce，
 * 那是我们发出去的，不会出现在这里）—— 名字写对，M2 才不会误用这个值。
 */
static void EapolPrintKeyFrame(const UINT8 *Body, UINT32 Available) {
    UINT16 KeyInformation = AicEapolGetBigEndian16(Body + AIC_EAPOL_KEY_INFORMATION_OFFSET);
    UINT16 MessageLength = AicEapolGetBigEndian16(Body + 2);
    UINT16 KeyLength = AicEapolGetBigEndian16(Body + AIC_EAPOL_KEY_LENGTH_OFFSET);
    UINT16 KeyDataLength = AicEapolGetBigEndian16(Body + WIFI_EAPOL_KEY_DATA_LENGTH_OFFSET);

    SdhciMark("WiFi: eapol rx len=");
    SdhciMarkDecimal("", MessageLength);
    SdhciMark(" key_info=0x");
    SdhciMarkHex("", KeyInformation, 4);
    SdhciMark(" replay=");
    AicEapolMarkBytes("", Body + AIC_EAPOL_REPLAY_OFFSET, AIC_EAPOL_REPLAY_LENGTH);
    SdhciMark(" anonce=");
    AicEapolMarkBytes("", Body + AIC_EAPOL_NONCE_OFFSET, AIC_EAPOL_NONCE_LENGTH);
    SdhciMark(" mic=");
    AicEapolMarkBytes("", Body + WIFI_EAPOL_MIC_OFFSET, WIFI_EAPOL_MIC_LENGTH);
    SdhciMark(" msg=");
    SdhciMark(EapolMessageName(KeyInformation));
    SdhciMark("\n");
    SdhciMark("WiFi: eapol rx: descriptor=");
    SdhciMarkDecimal("", (UINT32)Body[AIC_EAPOL_KEY_DESCRIPTOR_OFFSET]);
    SdhciMark(" key_version=");
    SdhciMarkDecimal("", (UINT32)KeyInformation & AIC_EAPOL_KEY_INFORMATION_VERSION_MASK);
    SdhciMark(" pairwise=");
    SdhciMarkDecimal("", (KeyInformation & AIC_EAPOL_KEY_INFORMATION_PAIRWISE) != 0u ? 1u : 0u);
    SdhciMark(" ack=");
    SdhciMarkDecimal("", (KeyInformation & AIC_EAPOL_KEY_INFORMATION_ACK) != 0u ? 1u : 0u);
    SdhciMark(" mic=");
    SdhciMarkDecimal("", (KeyInformation & AIC_EAPOL_KEY_INFORMATION_MIC) != 0u ? 1u : 0u);
    SdhciMark(" install=");
    SdhciMarkDecimal("", (KeyInformation & AIC_EAPOL_KEY_INFORMATION_INSTALL) != 0u ? 1u : 0u);
    SdhciMark(" secure=");
    SdhciMarkDecimal("", (KeyInformation & AIC_EAPOL_KEY_INFORMATION_SECURE) != 0u ? 1u : 0u);
    SdhciMark(" enc=");
    SdhciMarkDecimal("", (KeyInformation & AIC_EAPOL_KEY_INFORMATION_ENCRYPTED) != 0u ? 1u : 0u);
    SdhciMark(" error=");
    SdhciMarkDecimal("", (KeyInformation & AIC_EAPOL_KEY_INFORMATION_ERROR) != 0u ? 1u : 0u);
    SdhciMark(" key_len=");
    SdhciMarkDecimal("", KeyLength);
    SdhciMark(" kd_len=");
    SdhciMarkDecimal("", KeyDataLength);
    SdhciMark(" avail=");
    SdhciMarkDecimal("", Available - AIC_EAPOL_HEADER_LENGTH);
    SdhciMark(" msg=");
    SdhciMark(EapolMessageName(KeyInformation));
    SdhciMark("\n");
    if (KeyDataLength != 0u &&
        (UINT32)WIFI_EAPOL_KEY_DATA_OFFSET + KeyDataLength <= Available) {
        UINT32 Count =
            KeyDataLength > AIC_EAPOL_KEY_DATA_DUMP ? AIC_EAPOL_KEY_DATA_DUMP : KeyDataLength;

        SdhciMark("WiFi: eapol rx: key_data[");
        SdhciMarkDecimal("", Count);
        SdhciMark("/");
        SdhciMarkDecimal("", KeyDataLength);
        SdhciMark("]");
        AicEapolMarkBytes(" ", Body + WIFI_EAPOL_KEY_DATA_OFFSET, Count);
        SdhciMark("\n");
    }
}

/* EAPOL 头 + Key 体：先看 version/type/length，再决定要不要解 Key 字段 */
void AicEapolParseBody(const UINT8 *Body, UINT32 Available, const UINT8 *Station,
                       AIC_EAPOL_COUNTERS *Counters) {
    UINT32 Type = Body[1];

    if (Type != AIC_EAPOL_TYPE_KEY) {
        Counters->EapolFrames++;
        SdhciMark("WiFi: eapol rx: type=");
        SdhciMarkDecimal("", Type);
        SdhciMark(" (not key) version=");
        SdhciMarkDecimal("", (UINT32)Body[0]);
        SdhciMark(" len=");
        SdhciMarkDecimal("", AicEapolGetBigEndian16(Body + 2));
        SdhciMark(" -- skip\n");
        return;
    }
    Counters->EapolFrames++;
    Counters->KeyFrames++;
    if (Available < AIC_EAPOL_KEY_FIXED_LENGTH) {
        Counters->BadFrames++;
        SdhciMarkHex("WiFi: eapol rx: key frame too short avail=", Available, 4);
        SdhciMark(" (want >= 99)\n");
        return;
    }
    EapolPrintKeyFrame(Body, Available);
    /* 打印之后再交给 sink：串口上先看到这帧是什么，再看到握手模块用它做了什么 */
    if (gAicEapolKeySink != 0) {
        gAicEapolKeySink(Body, Available, Station);
    }
}

/*
 * 挂/摘"接住 Key 帧"的钩子。PR-34 只允许一个（四次握手是串行的）；
 * 传 0 就关掉，`wififw eapol poll` 与它互不影响。
 */
void AicEapolSetKeySink(AIC_EAPOL_KEY_SINK Sink) {
    gAicEapolKeySink = Sink;
}
