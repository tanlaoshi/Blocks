/*
 * WifiEapolParse.c — 数据帧 → 802.11/以太网 → 定位 EAPOL(0x888e) 的起点
 *
 * 用户眼里：`wififw eapol poll` 收到一帧数据后，这个文件回答"EAPOL 正文从第几字节开始"。
 * 从哪读：AicEapolParseData（唯一入口）→ EapolLocate（三种布局 + 裸扫兜底）
 *         → AicEapolParseBody（WifiEapolKey.c，解 Key 字段并打印）。
 * 别改：hw_rxhdr 60 B 的 MPDU 偏移来自厂商 rwnx_rx.c:2141 的 skb_pull(60)；
 *       802.11 data 头长 24/+2 QoS/+4 HTC/+6 4 地址来自 802.11-2016 §9.3.2.1。
 * 本刀不做：四次握手状态机、装密钥、派生 PMK/PTK、发 EAPOL（下一刀）。
 */
#include "WifiEapolPrivate.h"

/* LLC/SNAP：AA AA 03 | 00 00 00 | 88 8E（RFC 1042 + ETH_P_PAE） */
static const UINT8 sAicLlcSnapEapol[AIC_LLC_SNAP_LENGTH] = {
    0xAAu, 0xAAu, 0x03u, 0x00u, 0x00u, 0x00u, 0x88u, 0x8Eu
};

typedef struct {
    UINT32 Offset;       /* EAPOL 正文起点（0 = 没找到） */
    UINT32 HeaderLength; /* 802.11 头长，layout=80211 时有效 */
    UINT16 FrameControl; /* 802.11 帧控制字，layout=80211 时有效 */
    const char *Layout;  /* "80211" / "eth" / "scan" / "none" */
} AIC_EAPOL_LOCATION;

static UINT32 EapolGet32(const UINT8 *At) {
    return (UINT32)At[0] | ((UINT32)At[1] << 8) | ((UINT32)At[2] << 16) | ((UINT32)At[3] << 24);
}

static int EapolMatchSnap(const UINT8 *At) {
    UINT32 Index;

    for (Index = 0; Index < AIC_LLC_SNAP_LENGTH; Index++) {
        if (At[Index] != sAicLlcSnapEapol[Index]) {
            return 0;
        }
    }
    return 1;
}

/* 裸扫的兜底判据：SNAP 后面的 4 B 必须像个 EAPOL 头，避免在载荷里误命 */
static int EapolLooksLikeBody(const UINT8 *At, UINT32 Available) {
    UINT32 Version;
    UINT32 Type;
    UINT32 Length;

    if (Available < AIC_EAPOL_HEADER_LENGTH) {
        return 0;
    }
    Version = At[0];
    Type = At[1];
    Length = AicEapolGetBigEndian16(At + 2);
    if (Version < AIC_EAPOL_VERSION_2001 || Version > AIC_EAPOL_VERSION_2010) {
        return 0;
    }
    if (Type > AIC_EAPOL_TYPE_ASF_ALERT) {
        return 0;
    }
    if (Length > Available - AIC_EAPOL_HEADER_LENGTH) {
        return 0;
    }
    return 1;
}

/* 802.11 data 头长：24 + QoS 2 + HTC 4 + 4 地址 6。返回 0 = 不是 data 帧。
   PR-36：导出给 WifiEapolEther.c（解密帧定位 IP/ARP 时要跳同一个头）。 */
UINT32 AicEapolDot11HeaderLength(const UINT8 *Frame, UINT32 MpduLength) {
    const UINT8 *Mpdu = Frame + AIC_RX_HW_HEADER_LENGTH;
    UINT32 Header = AIC_DOT11_HEADER_LENGTH;

    if (MpduLength < AIC_DOT11_HEADER_LENGTH) {
        return 0u;
    }
    if ((Mpdu[0] & AIC_DOT11_FRAME_TYPE_MASK) != AIC_DOT11_FRAME_TYPE_DATA) {
        return 0u;
    }
    if ((Mpdu[0] & AIC_DOT11_FC0_QOS) != 0u) {
        Header += AIC_DOT11_QOS_LENGTH;
    }
    if ((Mpdu[1] & AIC_DOT11_FC1_HTC) != 0u) {
        Header += AIC_DOT11_HTC_LENGTH;
    }
    if ((Mpdu[1] & (AIC_DOT11_FC1_TO_DS | AIC_DOT11_FC1_FROM_DS)) ==
        (AIC_DOT11_FC1_TO_DS | AIC_DOT11_FC1_FROM_DS)) {
        Header += AIC_DOT11_ADDR4_LENGTH;
    }
    return Header;
}

/*
 * 三个候选布局，按可信度从高到低：
 *   1) 802.11 data：厂商原生长相（hw_rxhdr 60 + mac header + LLC/SNAP）
 *   2) 以太网：固件若已剥掉 802.11 头，ethertype 在 +12
 *   3) 裸扫 SNAP：偏移没校准时的兜底（会把命中偏移打出来，便于下次写死正确值）
 * 命中不了就 Offset=0，调用方打一行"没找到"继续。
 */
static void EapolLocate(const UINT8 *Frame, UINT32 MpduLength, UINT32 End,
                        AIC_EAPOL_LOCATION *Location) {
    const UINT8 *Mpdu = Frame + AIC_RX_HW_HEADER_LENGTH;
    UINT32 Header = AicEapolDot11HeaderLength(Frame, MpduLength);
    UINT32 Index;

    Location->Offset = 0u;
    Location->HeaderLength = 0u;
    Location->FrameControl = 0u;
    Location->Layout = "none";
    if (Header != 0u && Header + AIC_LLC_SNAP_LENGTH <= MpduLength &&
        EapolMatchSnap(Mpdu + Header) != 0) {
        Location->Offset = AIC_RX_HW_HEADER_LENGTH + Header + AIC_LLC_SNAP_LENGTH;
        Location->HeaderLength = Header;
        Location->FrameControl = (UINT16)((UINT32)Mpdu[0] | ((UINT32)Mpdu[1] << 8));
        Location->Layout = "80211";
        return;
    }
    if (MpduLength >= AIC_ETH_HEADER_LENGTH + AIC_EAPOL_HEADER_LENGTH &&
        AicEapolGetBigEndian16(Mpdu + 12) == AIC_ETHERTYPE_PAE) {
        Location->Offset = AIC_RX_HW_HEADER_LENGTH + AIC_ETH_HEADER_LENGTH;
        Location->Layout = "eth";
        return;
    }
    for (Index = 0; Index + AIC_LLC_SNAP_LENGTH + AIC_EAPOL_HEADER_LENGTH <= End; Index++) {
        if (EapolMatchSnap(Frame + Index) == 0) {
            continue;
        }
        if (EapolLooksLikeBody(Frame + Index + AIC_LLC_SNAP_LENGTH,
                               End - Index - AIC_LLC_SNAP_LENGTH) == 0) {
            continue;
        }
        Location->Offset = Index + AIC_LLC_SNAP_LENGTH;
        Location->Layout = "scan";
        return;
    }
}

/*
 * 帧级校验：读 hw_rxhdr、打第一行、fcs 检查、把 hwvect.len 夹进本块长度。
 * 返回 0 = 可以继续（End/Decryption 已填）；-1 = 已经打印并计数，调用方直接 return。
 */
static int EapolCheckDataFrame(const UINT8 *Frame, UINT32 FrameLength, UINT32 *OutEnd,
                               UINT32 *OutDecryption, AIC_EAPOL_COUNTERS *Counters) {
    UINT32 Status;
    UINT32 Flags;
    UINT32 MpduLength;
    UINT32 End;

    if (FrameLength < AIC_RX_HW_HEADER_LENGTH) {
        SdhciMarkHex("WiFi: eapol: data frame shorter than hw hdr, len=", FrameLength, 4);
        SdhciMark("\n");
        Counters->BadFrames++;
        return -1;
    }
    MpduLength = (UINT32)Frame[0] | ((UINT32)Frame[1] << 8);
    Status = EapolGet32(Frame + AIC_RX_HW_STATUS_OFFSET);
    Flags = EapolGet32(Frame + AIC_RX_HW_FLAGS_OFFSET);
    *OutDecryption = (Status >> AIC_RX_HW_STATUS_DECR_SHIFT) & AIC_RX_HW_STATUS_DECR_MASK;
    SdhciMarkHex("WiFi: eapol data: len=", FrameLength, 4);
    SdhciMarkHex(" hwlen=", MpduLength, 4);
    SdhciMarkHex(" type=0x", (UINT32)Frame[AIC_SDIO_TYPE_OFFSET], 2);
    SdhciMarkHex(" vif=", (Flags >> AIC_RX_HW_FLAGS_INDEX_SHIFT) & AIC_RX_HW_FLAGS_INDEX_MASK,
                 2);
    SdhciMarkHex(" sta=", (Flags >> AIC_RX_HW_FLAGS_STA_SHIFT) & AIC_RX_HW_FLAGS_INDEX_MASK, 2);
    SdhciMarkHex(" status=", Status, 8);
    SdhciMarkHex(" decr=", *OutDecryption, 2);
    SdhciMark("\n");
    if (Counters->HeadDumped == 0u) {
        AicEapolDumpHead(Frame, FrameLength);
        Counters->HeadDumped = 1u;
    }
    if ((Status & AIC_RX_HW_STATUS_FCS_ERR) != 0u) {
        SdhciMark("WiFi: eapol: fcs_err=1 (CRC bad), drop this frame\n");
        Counters->BadFrames++;
        return -1;
    }
    if (MpduLength == 0u) {
        SdhciMark("WiFi: eapol: hwlen=0 (bad frame), drop\n");
        Counters->BadFrames++;
        return -1;
    }
    End = FrameLength - AIC_RX_HW_HEADER_LENGTH;
    if (MpduLength > End) {
        SdhciMarkHex("WiFi: eapol: hwlen out of range, clamp=", End, 4);
        SdhciMark("\n");
        Counters->BadFrames++;
        MpduLength = End;
    }
    *OutEnd = AIC_RX_HW_HEADER_LENGTH + MpduLength;
    if (*OutEnd > FrameLength) {
        *OutEnd = FrameLength;
    }
    return 0;
}

/* 打印这帧命中的布局（802.11 头长 + 帧控制字），偏移不认得时不打 */
static void EapolPrintLocation(const AIC_EAPOL_LOCATION *Location) {
    SdhciMark("WiFi: eapol: layout=");
    SdhciMark(Location->Layout);
    SdhciMarkHex(" off=", Location->Offset, 4);
    if (Location->HeaderLength != 0u) {
        SdhciMarkHex(" dot11_hdr=", Location->HeaderLength, 2);
        SdhciMarkHex(" fc=0x", (UINT32)Location->FrameControl, 4);
    }
    SdhciMark("\n");
}

/*
 * 数据帧总入口。任何一步不认识都只打一行、返回 —— 这里是"不许崩"的边界：
 * 帧短、hwlen 越界、fcs 错、加密帧、没有 SNAP、EAPOL 太短，各有一条独立文案。
 */
void AicEapolParseData(const UINT8 *Frame, UINT32 FrameLength, AIC_EAPOL_COUNTERS *Counters) {
    AIC_EAPOL_LOCATION Location;
    const UINT8 *Station = 0;
    UINT32 Decryption = 0u;
    UINT32 MpduLength;
    UINT32 End = 0u;

    if (EapolCheckDataFrame(Frame, FrameLength, &End, &Decryption, Counters) != 0) {
        return;
    }
    if (Decryption != AIC_RX_HW_DECR_UNENC) {
        Counters->EncryptedFrames++;
        /*
         * decr != 0 = 固件已经把这一帧解开了（CCMP/TKIP）。装钥匙之前不该出现它，
         * 出现就是"钥匙已装、数据通路活了"的硬证据 —— 这正是本刀要的那一行。
         * 打印封顶：组播/广播一旦开始流，逐帧打会把串口刷满（后面只计数）。
         */
        if (gAicEapolKeysInstalled != 0u) {
            if (Counters->EncryptedFrames <= AIC_EAPOL_DATA_AFTER_KEYS_LOG) {
                SdhciMark("WiFi: data frame after keys: len=");
                SdhciMarkDecimal("", FrameLength);
                SdhciMark(" decr=");
                SdhciMarkDecimal("", Decryption);
                SdhciMark(" (the firmware decrypted it: PTK/GTK match the AP)\n");
                /* 诊断：把解密后帧的前 16 B 打出来，用来定位 ethertype 到底在哪个偏移
                   （DHCP OFFER 收不到时，先看"帧有没有来"再看"偏对不对"）。 */
                if (Counters->EncryptedFrames <= AIC_EAPOL_DATA_AFTER_KEYS_DUMP) {
                    SdhciMark("WiFi: data after keys: bytes@60 ");
                    AicDumpBytes(Frame + AIC_RX_HW_HEADER_LENGTH,
                                 FrameLength > AIC_RX_HW_HEADER_LENGTH + 32u
                                     ? 16u : (FrameLength > AIC_RX_HW_HEADER_LENGTH ? 8u : 0u));
                }
            }
        } else {
            SdhciMarkHex("WiFi: eapol: encrypted data frame decr=", Decryption, 2);
            SdhciMark(" (no LLC/SNAP visible; keys not installed yet)\n");
        }
        /* PR-36：解密帧里的 IP/ARP 交给 DHCP/ARP 处理（EAPOL 已在上面的分支外） */
        AicEapolDispatchEther(Frame, End, Decryption);
        return;
    }
    MpduLength = End - AIC_RX_HW_HEADER_LENGTH;
    EapolLocate(Frame, MpduLength, End, &Location);
    /*
     * addr1 = 这帧的目的 MAC；AP 单播给我们时就是本机 STA MAC（PTK 要用）。
     * 只有 layout=80211 的命中才有 802.11 头（HeaderLength 非 0）；eth/scan 布局拿不到，
     * 传 0 让上层自己判断"这帧没带 STA MAC"。
     */
    if (Location.HeaderLength != 0u) {
        Station = Frame + AIC_RX_HW_HEADER_LENGTH + AIC_DOT11_ADDR1_OFFSET;
    }
    if (Location.Offset == 0u) {
        SdhciMark("WiFi: eapol: no LLC/SNAP 0x888e in this data frame (see hdr/mpdu above)\n");
        /* 明文（decr=0）的 IP/ARP 也照样分流：开放网络/不加密的广播会走这里 */
        AicEapolDispatchEther(Frame, End, Decryption);
    } else if (Location.Offset + AIC_EAPOL_HEADER_LENGTH > End) {
        SdhciMark("WiFi: eapol: body cut by frame end, drop\n");
        Counters->BadFrames++;
    } else {
        EapolPrintLocation(&Location);
        AicEapolParseBody(Frame + Location.Offset, End - Location.Offset, Station, Counters);
    }
    /*
     * 不再"裸扫块尾"：一个 SDIO 块里的后续帧现在由 WifiEapol.c 的 EapolDispatchBlock
     * 按厂商的聚合帧算术逐帧解（aicwf_txrxif.c:290-350），块尾不再需要猜偏移。
     */
}
