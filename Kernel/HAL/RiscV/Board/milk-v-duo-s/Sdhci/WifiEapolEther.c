/*
 * WifiEapolEther.c — PR-36：把解密数据帧里的**以太网载荷**（IP/ARP）分流出去
 *
 * 用户眼里：`wififw dhcp` 收到的 OFFER/ACK、`WiFi: arp: reply …` —— 它们都是固件
 *           解密之后的数据帧，这里负责从 802.11 头 + 加密头 + LLC/SNAP 里把载荷抠出来。
 * 从哪读：AicEapolDispatchEther（唯一入口，WifiEapolParse.c 每帧调一次）→
 *         AicEapolLocateEthernet（三种候选偏移）→ gAicEapolEtherSink（DHCP/ARP 的口）。
 * 别改：解密帧的布局是厂商契约：`[hw_rxhdr 60][802.11 头][加密头 4/8/18][LLC/SNAP 8][载荷]`
 *       （rwnx_rx.c:2141 的 skb_pull(60)、:2167-2186 的 pull_len、:2177 的 ethertype
 *       取法）。**不另写 SDIO 收包**：帧和分流点都在 WifiEapolParse.c 里，这里只做定位。
 * 兜底：decr 报告的值不可信时把"没有加密头"和"8 B 加密头"各试一次；固件若已经把
 *       802.11 头剥掉，就退回以太网布局（ethertype 在 +12，RFC 894 §2）。
 */
#include "WifiEapolPrivate.h"

AIC_WIFI_ETHER_SINK gAicEapolEtherSink;

void AicEapolSetEtherSink(AIC_WIFI_ETHER_SINK Sink) {
    gAicEapolEtherSink = Sink;
}

/* 加密头长：厂商 rwnx_rx.c:2167-2186 的 pull_len 里 CCMP/TKIP +8、WEP +4、WAPI +18 */
static UINT32 EapolSecurityHeaderLength(UINT32 Decryption) {
    if (Decryption == AIC_RX_HW_DECR_WEP) {
        return AIC_RX_SEC_HEADER_WEP;
    }
    if (Decryption == AIC_RX_HW_DECR_TKIP || Decryption == AIC_RX_HW_DECR_CCMP) {
        return AIC_RX_SEC_HEADER_CCMP_TKIP;
    }
    if (Decryption == AIC_RX_HW_DECR_WAPI) {
        return AIC_RX_SEC_HEADER_WAPI;
    }
    return 0u;
}

/* LLC/SNAP 头的前 6 B：AA AA 03 | OUI 00 00 00；ethertype 在 +6（RFC 1042 §2） */
static int EapolMatchSnapOui(const UINT8 *At) {
    static const UINT8 SnapOui[AIC_LLC_SNAP_LENGTH - 2u] = {
        0xAAu, 0xAAu, 0x03u, 0x00u, 0x00u, 0x00u};
    UINT32 Index;

    for (Index = 0; Index < AIC_LLC_SNAP_LENGTH - 2u; Index++) {
        if (At[Index] != SnapOui[Index]) {
            return 0;
        }
    }
    return 1;
}

/*
 * 三个候选偏移，按可信度从高到低：
 *   1) 802.11 头 + 加密头（厂商布局，decr 决定加密头长）
 *   2) 802.11 头（decr 报告不可信 / 固件已剥掉加密头）
 *   3) 802.11 头 + 8（另一种 8 B 加密头，CCMP 与 TKIP 都是 8）
 * 命中判据是 LLC/SNAP 前 6 B —— 误命中概率极低，且上层还要过 IP/UDP/DHCP 的校验。
 */
static int EapolLocateEthernet(const UINT8 *Frame, UINT32 End, UINT32 Decryption,
                               UINT16 *OutEtherType, const UINT8 **OutPayload, UINT32 *OutLength) {
    const UINT8 *Mpdu = Frame + AIC_RX_HW_HEADER_LENGTH;
    UINT32 MpduLength = End - AIC_RX_HW_HEADER_LENGTH;
    UINT32 Header = AicEapolDot11HeaderLength(Frame, MpduLength);
    UINT32 Offsets[AIC_ETH_LOCATE_CANDIDATES];
    UINT32 Count = 0;
    UINT32 Index;

    if (Header == 0u) {
        /* 不是 802.11 data 帧：按"固件已经剥到以太网"处理（ethertype 在 +12） */
        if (MpduLength < AIC_ETH_HEADER_LENGTH + 1u) {
            return 0;
        }
        *OutEtherType = AicEapolGetBigEndian16(Mpdu + AIC_ETH_HEADER_LENGTH - 2u);
        *OutPayload = Mpdu + AIC_ETH_HEADER_LENGTH;
        *OutLength = MpduLength - AIC_ETH_HEADER_LENGTH;
        return 1;
    }
    Offsets[Count++] = Header + EapolSecurityHeaderLength(Decryption);
    Offsets[Count++] = Header;
    Offsets[Count++] = Header + AIC_RX_SEC_HEADER_CCMP_TKIP;
    for (Index = 0; Index < Count; Index++) {
        UINT32 Offset = Offsets[Index];

        if (Offset + AIC_LLC_SNAP_LENGTH > MpduLength) {
            continue;
        }
        if (EapolMatchSnapOui(Mpdu + Offset) == 0) {
            continue;
        }
        *OutEtherType = AicEapolGetBigEndian16(Mpdu + Offset + AIC_LLC_SNAP_LENGTH - 2u);
        *OutPayload = Mpdu + Offset + AIC_LLC_SNAP_LENGTH;
        *OutLength = MpduLength - Offset - AIC_LLC_SNAP_LENGTH;
        return 1;
    }
    return 0;
}

/* WifiEapolParse.c 每解完一帧调一次；没挂 sink 就是空操作（eapol poll/join 不受影响） */
void AicEapolDispatchEther(const UINT8 *Frame, UINT32 End, UINT32 Decryption) {
    const UINT8 *Payload = 0;
    UINT32 Length = 0;
    UINT16 EtherType = 0;

    if (gAicEapolEtherSink == 0 || End <= AIC_RX_HW_HEADER_LENGTH) {
        return;
    }
    if (EapolLocateEthernet(Frame, End, Decryption, &EtherType, &Payload, &Length) == 0) {
        return;
    }
    gAicEapolEtherSink(EtherType, Payload, Length, Decryption);
}
