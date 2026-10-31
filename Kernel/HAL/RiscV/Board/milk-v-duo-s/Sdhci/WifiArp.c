/*
 * WifiArp.c — PR-36：ARP 请求/应答（"拿到 IP 之后问网关"的更强判据）
 *
 * 用户眼里：`WiFi: arp: request who-has 192.168.x.1 tell 192.168.x.y` 之后收到
 *           `WiFi: arp: reply 192.168.x.1 is-at aa:bb:…` —— 说明**双向**数据通路都活了。
 * 从哪读：AicArpResolveGateway（发 + 等）→ AicArpBuildRequest / AicArpParseReply
 *         （帧格式）→ AicArpHandlePacket（收包口，挂在 WifiNetFrame.c 的 sink 上）。
 * 别改：ARP 的字段顺序/宽度来自 RFC 826 的包格式；请求的 tha 无意义，填 0；
 *       以太网最小帧 60 B（不含 FCS，802.3 §3.2.7），ARP 只有 42 B，其余补 0。
 * 为什么只问网关：DHCP 只证明"我们能发、服务器能广播回来"；ARP 是单播往返，
 *       同时验证了 PTK（单播）与对端地址，比 DHCP 广播更能说明通路是双向的。
 */
#include "WifiNetPrivate.h"
#include "CString.h"

/* 一次解析/等待的状态：只服务当前这条命令，sAicArpActive 为 0 时不参与分流 */
static UINT8 sAicArpFrame[AIC_NET_FRAME_MAX];
static UINT8 sAicArpTargetIp[AIC_NET_IPV4_LENGTH];
static UINT8 sAicArpTargetMac[AIC_NET_MAC_LENGTH];
static UINT32 sAicArpActive;
static UINT32 sAicArpReplySeen;

/* 拼一帧 ARP request（42 B 有效 + 补齐到 60 B），返回以太网帧长 */
UINT32 AicArpBuildRequest(UINT8 *Frame, UINT32 Capacity, const UINT8 *Station,
                          const UINT8 *StationIp, const UINT8 *TargetIp) {
    UINT32 Length = AIC_NET_ETHERNET_HEADER_LENGTH + AIC_NET_ARP_PACKET_LENGTH;
    UINT8 *Arp;
    UINT32 Index;

    if (Frame == 0 || Station == 0 || StationIp == 0 || TargetIp == 0) {
        return 0u;
    }
    if (Capacity < AIC_NET_ETHERNET_MIN_FRAME || Capacity < Length) {
        return 0u;
    }
    Arp = AicNetAddEthernetHeader(Frame, gAicNetBroadcastMac, Station, AIC_NET_ETHERTYPE_ARP);
    AicNetPutBe16(Arp + AIC_NET_ARP_HARDWARE_TYPE_OFFSET, AIC_NET_ARP_HARDWARE_ETHERNET);
    AicNetPutBe16(Arp + AIC_NET_ARP_PROTOCOL_TYPE_OFFSET, AIC_NET_ARP_PROTOCOL_IPV4);
    Arp[AIC_NET_ARP_HARDWARE_LENGTH_OFFSET] = AIC_NET_ARP_HARDWARE_LENGTH;
    Arp[AIC_NET_ARP_PROTOCOL_LENGTH_OFFSET] = AIC_NET_ARP_PROTOCOL_LENGTH;
    AicNetPutBe16(Arp + AIC_NET_ARP_OPERATION_OFFSET, AIC_NET_ARP_OPERATION_REQUEST);
    (void)memcpy(Arp + AIC_NET_ARP_SENDER_HARDWARE_OFFSET, Station, AIC_NET_MAC_LENGTH);
    (void)memcpy(Arp + AIC_NET_ARP_SENDER_PROTOCOL_OFFSET, StationIp, AIC_NET_IPV4_LENGTH);
    for (Index = 0; Index < AIC_NET_MAC_LENGTH; Index++) {
        Arp[AIC_NET_ARP_TARGET_HARDWARE_OFFSET + Index] = 0u; /* tha 未知（RFC 826） */
    }
    (void)memcpy(Arp + AIC_NET_ARP_TARGET_PROTOCOL_OFFSET, TargetIp, AIC_NET_IPV4_LENGTH);
    /* 80 字节的容量里 42 B 有效；补到 60 B 的以太网最小帧（不含 FCS） */
    for (Index = Length; Index < AIC_NET_ETHERNET_MIN_FRAME; Index++) {
        Frame[Index] = 0u;
    }
    return AIC_NET_ETHERNET_MIN_FRAME;
}

/* 1 = 这是问 TargetIp 的 ARP reply；sender 的 MAC 填进 OutMac */
int AicArpParseReply(const UINT8 *Payload, UINT32 Length, const UINT8 *TargetIp, UINT8 *OutMac) {
    if (Payload == 0 || TargetIp == 0 || OutMac == 0 || Length < AIC_NET_ARP_PACKET_LENGTH) {
        return 0;
    }
    if (AicNetGetBe16(Payload + AIC_NET_ARP_HARDWARE_TYPE_OFFSET) !=
        AIC_NET_ARP_HARDWARE_ETHERNET) {
        return 0;
    }
    if (AicNetGetBe16(Payload + AIC_NET_ARP_PROTOCOL_TYPE_OFFSET) != AIC_NET_ARP_PROTOCOL_IPV4) {
        return 0;
    }
    if (Payload[AIC_NET_ARP_HARDWARE_LENGTH_OFFSET] != AIC_NET_ARP_HARDWARE_LENGTH ||
        Payload[AIC_NET_ARP_PROTOCOL_LENGTH_OFFSET] != AIC_NET_ARP_PROTOCOL_LENGTH) {
        return 0;
    }
    if (AicNetGetBe16(Payload + AIC_NET_ARP_OPERATION_OFFSET) != AIC_NET_ARP_OPERATION_REPLY) {
        return 0;
    }
    if (memcmp(Payload + AIC_NET_ARP_SENDER_PROTOCOL_OFFSET, TargetIp, AIC_NET_IPV4_LENGTH) != 0) {
        return 0;
    }
    (void)memcpy(OutMac, Payload + AIC_NET_ARP_SENDER_HARDWARE_OFFSET, AIC_NET_MAC_LENGTH);
    return 1;
}

/* 收包口：只在 AicArpResolveGateway 等待期间收；第一条合法 reply 就落定 */
void AicArpHandlePacket(const UINT8 *Payload, UINT32 Length, UINT32 Decryption) {
    UINT8 Mac[AIC_NET_MAC_LENGTH];

    (void)Decryption; /* ARP 的 decr 由 AicNetPollRun 的调用者看到，这里不重复打印 */
    if (sAicArpActive == 0u || sAicArpReplySeen != 0u) {
        return;
    }
    if (AicArpParseReply(Payload, Length, sAicArpTargetIp, Mac) == 0) {
        return;
    }
    (void)memcpy(sAicArpTargetMac, Mac, AIC_NET_MAC_LENGTH);
    sAicArpReplySeen = 1u;
}

static int ArpReplyDone(void) {
    return (int)sAicArpReplySeen;
}

/*
 * 问网关的 MAC：发 REQUEST → 等 REPLY。两次尝试（每次等预算的一半），失败只报一行，
 * 不影响 DHCP 已经拿到的地址（ARP 是"更强判据"，不是 DHCP 的前置条件）。
 */
int AicArpResolveGateway(SDHCI_HOST *Host, const UINT8 *Station, const UINT8 *StationIp,
                         const UINT8 *GatewayIp, UINT8 VifIndex, UINT8 ApIndex, UINT8 *OutMac,
                         UINT32 BudgetMs) {
    UINT32 Length;
    UINT32 Try;
    UINT32 Slice;
    char GatewayText[AIC_NET_IPV4_TEXT_MAX];
    char StationText[AIC_NET_IPV4_TEXT_MAX];

    if (Host == 0 || Station == 0 || StationIp == 0 || GatewayIp == 0 || OutMac == 0) {
        return -1;
    }
    Length = AicArpBuildRequest(sAicArpFrame, (UINT32)sizeof(sAicArpFrame), Station, StationIp,
                                GatewayIp);
    if (Length == 0u) {
        SdhciMark("WiFi: arp: build request failed\n");
        return -1;
    }
    (void)memcpy(sAicArpTargetIp, GatewayIp, AIC_NET_IPV4_LENGTH);
    sAicArpReplySeen = 0u;
    sAicArpActive = 1u;
    AicNetFormatIpv4(GatewayIp, GatewayText);
    AicNetFormatIpv4(StationIp, StationText);
    Slice = BudgetMs / AIC_NET_ARP_TRIES;
    if (Slice == 0u) {
        Slice = 1u;
    }
    for (Try = 0; Try < AIC_NET_ARP_TRIES; Try++) {
        SdhciMark("WiFi: arp: request who-has ");
        SdhciMark(GatewayText);
        SdhciMark(" tell ");
        SdhciMark(StationText);
        SdhciMark("\n");
        if (AicWifiDataSend(Host, sAicArpFrame, Length, VifIndex, ApIndex) != 0) {
            sAicArpActive = 0u;
            SdhciMark("WiFi: arp: request tx failed (see the data tx line above)\n");
            return -1;
        }
        if (AicNetPollRun(Host, Slice, ArpReplyDone) == 0) {
            break;
        }
    }
    sAicArpActive = 0u;
    if (sAicArpReplySeen == 0u) {
        SdhciMark("WiFi: arp: no reply from ");
        SdhciMark(GatewayText);
        SdhciMark(" (DHCP ok but the unicast/tx path may be one-way)\n");
        return -1;
    }
    (void)memcpy(OutMac, sAicArpTargetMac, AIC_NET_MAC_LENGTH);
    SdhciMark("WiFi: arp: reply ");
    SdhciMark(GatewayText);
    SdhciMark(" is-at ");
    AicNetMarkMac("", OutMac);
    SdhciMark("\n");
    return 0;
}

/*
 * 探针：把 ARP 请求**单播**发给指定 MAC（一般是 AP 自己的 BSSID）。
 * 动机：DHCP 要靠广播，而广播在我们这里被固件用 PTK 加密（AP 丢）。
 * 单播走 PTK 是正确的，所以先用"单播 ARP 问 AP 自己的 IP"验证单播数据通路。
 * 成功即证明：装密钥后 unicast 双向可通，后续可以用静态 IP 直接通信。
 */
int AicArpResolveUnicast(SDHCI_HOST *Host, const UINT8 *Station, const UINT8 *StationIp,
                         const UINT8 *TargetIp, const UINT8 *DstMac, UINT8 VifIndex,
                         UINT8 ApIndex, UINT8 *OutMac, UINT32 BudgetMs) {
    UINT32 Length;
    UINT32 Try;
    UINT32 Slice;
    char TargetText[AIC_NET_IPV4_TEXT_MAX];

    if (Host == 0 || Station == 0 || StationIp == 0 || TargetIp == 0 || DstMac == 0 ||
        OutMac == 0) {
        return -1;
    }
    Length = AicArpBuildRequest(sAicArpFrame, (UINT32)sizeof(sAicArpFrame), Station, StationIp,
                                TargetIp);
    if (Length == 0u) {
        SdhciMark("WiFi: arp uni: build failed\n");
        return -1;
    }
    /* 关键：目的 MAC 从广播改成 BSSID（单播） */
    for (UINT32 i = 0; i < AIC_NET_MAC_LENGTH; i++) {
        sAicArpFrame[i] = DstMac[i];
    }
    (void)memcpy(sAicArpTargetIp, TargetIp, AIC_NET_IPV4_LENGTH);
    sAicArpReplySeen = 0u;
    sAicArpActive = 1u;
    AicNetFormatIpv4(TargetIp, TargetText);
    Slice = BudgetMs / AIC_NET_ARP_TRIES;
    if (Slice == 0u) {
        Slice = 1u;
    }
    for (Try = 0; Try < AIC_NET_ARP_TRIES; Try++) {
        SdhciMark("WiFi: arp uni: who-has ");
        SdhciMark(TargetText);
        SdhciMark(" (unicast to the AP)\n");
        if (AicWifiDataSend(Host, sAicArpFrame, Length, VifIndex, ApIndex) != 0) {
            sAicArpActive = 0u;
            SdhciMark("WiFi: arp uni: tx failed\n");
            return -1;
        }
        if (AicNetPollRun(Host, Slice, ArpReplyDone) == 0) {
            break;
        }
    }
    sAicArpActive = 0u;
    if (sAicArpReplySeen == 0u) {
        SdhciMark("WiFi: arp uni: no reply (unicast path or key is wrong)\n");
        return -1;
    }
    (void)memcpy(OutMac, sAicArpTargetMac, AIC_NET_MAC_LENGTH);
    SdhciMark("WiFi: arp uni: reply ok — unicast data path works\n");
    return 0;
}
