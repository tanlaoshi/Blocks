/*
 * WifiNetFrame.c — PR-36：DHCP/ARP 共用的帧公共层（字节序、校验和、IP/UDP 头、收包口）
 *
 * 用户眼里：`wififw dhcp` 发的每一帧都从这里拼出以太网/IP/UDP 三层头；收到的解密帧
 *           也由这里按 ethertype 分流到 DHCP 或 ARP。
 * 从哪读：AicNetLayoutCheck（布局自检）→ AicNetBuildIpv4UdpFrame（组头）→
 *         AicNetChecksum（IPv4 头校验和）→ AicNetFindUdpPayload（收帧定位）→
 *         AicNetPollRun（在 EAPOL 收包路径上等条件）。
 * 别改：网络字节序由 AicNetPutBe16/32 显式写；**不能用结构体赋值**（RISC-V 是小端，
 *       RFC 791/768/2131 的字段都是大端）。收包复用 WifiEapol.c 的逐帧路径，不另开 SDIO。
 * 来源：RFC 791 §3.1、RFC 768、RFC 894 §2、RFC 2131 §2、RFC 826；厂商收包布局见
 *       rwnx_rx.c:2141（skb_pull 60）与 :2205（解密帧的加密头 + LLC/SNAP）。
 */
#include "WifiNetPrivate.h"
#include "CString.h"
#include "Compiler.h" /* BLOCKS_OFFSETOF（与 WifiEapol.c 的布局自检同一套） */

/* DHCP/ARP 共用的固定地址：广播 MAC/IP、全零源 IP（DHCP 发现阶段还没有地址） */
const UINT8 gAicNetBroadcastMac[AIC_NET_MAC_LENGTH] = {
    0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu};
const UINT8 gAicNetBroadcastIp[AIC_NET_IPV4_LENGTH] = {0xFFu, 0xFFu, 0xFFu, 0xFFu};
const UINT8 gAicNetZeroIp[AIC_NET_IPV4_LENGTH] = {0u, 0u, 0u, 0u};

/* 布局核对表：自建镜像的 sizeof/offsetof vs RFC 定死的字节数 */
typedef struct {
    const char *Name;
    UINT32 Actual;
    UINT32 Expected;
} AIC_NET_LAYOUT_ITEM;

static const AIC_NET_LAYOUT_ITEM sAicNetLayout[] = {
    {"ethernet", (UINT32)sizeof(AIC_NET_ETHERNET_HEADER), AIC_NET_ETHERNET_HEADER_LENGTH},
    {"ethernet.ethertype", (UINT32)BLOCKS_OFFSETOF(AIC_NET_ETHERNET_HEADER, EtherType),
     AIC_NET_ETHERTYPE_OFFSET},
    {"ipv4", (UINT32)sizeof(AIC_NET_IPV4_HEADER), AIC_NET_IPV4_HEADER_LENGTH},
    {"ipv4.checksum", (UINT32)BLOCKS_OFFSETOF(AIC_NET_IPV4_HEADER, HeaderChecksum),
     AIC_NET_IPV4_CHECKSUM_OFFSET},
    {"ipv4.source", (UINT32)BLOCKS_OFFSETOF(AIC_NET_IPV4_HEADER, Source),
     AIC_NET_IPV4_SOURCE_OFFSET},
    {"ipv4.destination", (UINT32)BLOCKS_OFFSETOF(AIC_NET_IPV4_HEADER, Destination),
     AIC_NET_IPV4_DESTINATION_OFFSET},
    {"udp", (UINT32)sizeof(AIC_NET_UDP_HEADER), AIC_NET_UDP_HEADER_LENGTH},
    {"dhcp.fixed", (UINT32)sizeof(AIC_NET_DHCP_FIXED), AIC_NET_DHCP_OPTIONS_OFFSET},
    {"dhcp.magic", (UINT32)BLOCKS_OFFSETOF(AIC_NET_DHCP_FIXED, MagicCookie),
     AIC_NET_DHCP_MAGIC_OFFSET},
    {"dhcp.your_ip", (UINT32)BLOCKS_OFFSETOF(AIC_NET_DHCP_FIXED, YourIp),
     AIC_NET_DHCP_YOUR_IP_OFFSET},
    /* chaddr 是 16 B 定长字段（RFC 2131 §2），不是 6 B 的 MAC —— 只核对镜像宽度 */
    {"dhcp.chaddr", (UINT32)sizeof(((AIC_NET_DHCP_FIXED *)0)->ClientHardware),
     AIC_NET_DHCP_CLIENT_HARDWARE_LENGTH},
    {"arp", (UINT32)sizeof(AIC_NET_ARP_PACKET), AIC_NET_ARP_PACKET_LENGTH},
    {"arp.sender_hw", (UINT32)BLOCKS_OFFSETOF(AIC_NET_ARP_PACKET, SenderHardware),
     AIC_NET_ARP_SENDER_HARDWARE_OFFSET},
    {"arp.target_proto", (UINT32)BLOCKS_OFFSETOF(AIC_NET_ARP_PACKET, TargetProtocol),
     AIC_NET_ARP_TARGET_PROTOCOL_OFFSET},
};

#define AIC_NET_LAYOUT_COUNT ((UINT32)(sizeof(sAicNetLayout) / sizeof(sAicNetLayout[0])))

/* 一条不匹配就停：排歪了说明编译器把镜像排歪了，发出去的字段会整体错位 */
int AicNetLayoutCheck(void) {
    UINT32 Index;
    int Bad = 0;

    for (Index = 0; Index < AIC_NET_LAYOUT_COUNT; Index++) {
        const AIC_NET_LAYOUT_ITEM *Item = &sAicNetLayout[Index];

        if (Item->Actual == Item->Expected) {
            continue;
        }
        SdhciMark("WiFi: net struct layout BAD: ");
        SdhciMark(Item->Name);
        SdhciMarkHex(" got=", Item->Actual, 4);
        SdhciMarkHex(" want=", Item->Expected, 4);
        SdhciMark("\n");
        Bad = 1;
    }
    if (Bad != 0) {
        SdhciMark("WiFi: net struct layout check failed, refuse to send\n");
        return -1;
    }
    return 0;
}

/* 线上字节序一律大端（RFC 791/768/2131），显式按字节写，不依赖 CPU 端序 */
UINT16 AicNetGetBe16(const UINT8 *At) {
    return (UINT16)(((UINT16)At[0] << 8) | (UINT16)At[1]);
}

UINT32 AicNetGetBe32(const UINT8 *At) {
    return ((UINT32)At[0] << 24) | ((UINT32)At[1] << 16) | ((UINT32)At[2] << 8) | (UINT32)At[3];
}

void AicNetPutBe16(UINT8 *At, UINT16 Value) {
    At[0] = (UINT8)((Value >> 8) & 0xFFu);
    At[1] = (UINT8)(Value & 0xFFu);
}

void AicNetPutBe32(UINT8 *At, UINT32 Value) {
    At[0] = (UINT8)((Value >> 24) & 0xFFu);
    At[1] = (UINT8)((Value >> 16) & 0xFFu);
    At[2] = (UINT8)((Value >> 8) & 0xFFu);
    At[3] = (UINT8)(Value & 0xFFu);
}

/*
 * 反码求和再取反（RFC 1071 §1 的 16 位字算法）：IP 头校验和、UDP 伪头都用它。
 * 奇数长度时最后一个字节按高字节补 0（RFC 1071 §2）。
 */
UINT16 AicNetChecksum(const UINT8 *Data, UINT32 Length) {
    UINT32 Sum = 0;
    UINT32 Index;

    for (Index = 0; Index + 1u < Length; Index += 2u) {
        Sum += ((UINT32)Data[Index] << 8) | (UINT32)Data[Index + 1u];
    }
    if ((Length & 1u) != 0u) {
        Sum += (UINT32)Data[Length - 1u] << 8;
    }
    while ((Sum >> 16) != 0u) {
        Sum = (Sum & 0xFFFFu) + (Sum >> 16);
    }
    return (UINT16)(~Sum & 0xFFFFu);
}

/* 写 14 B 以太网头，返回帧内载荷起点（RFC 894 §2） */
UINT8 *AicNetAddEthernetHeader(UINT8 *Frame, const UINT8 *Destination, const UINT8 *Source,
                               UINT16 EtherType) {
    (void)memcpy(Frame, Destination, AIC_NET_MAC_LENGTH);
    (void)memcpy(Frame + AIC_NET_MAC_LENGTH, Source, AIC_NET_MAC_LENGTH);
    AicNetPutBe16(Frame + AIC_NET_ETHERTYPE_OFFSET, EtherType);
    return Frame + AIC_NET_ETHERNET_HEADER_LENGTH;
}

/*
 * eth + IPv4 + UDP 三层头：IP 总长 / UDP 长度 / IP 头校验和都由 PayloadLength 算出来。
 * UDP 校验和写 0（RFC 768："the checksum is optional" 对 IPv4；0 表示未计算）。
 * 载荷区被清零，调用方直接在返回指针上写 DHCP/ARP 正文，补齐位就是 0。
 */
UINT8 *AicNetBuildIpv4UdpFrame(UINT8 *Frame, const UINT8 *Destination, const UINT8 *Source,
                               const UINT8 *SourceIp, const UINT8 *DestinationIp,
                               UINT16 SourcePort, UINT16 DestinationPort,
                               UINT32 PayloadLength) {
    UINT8 *Ip = AicNetAddEthernetHeader(Frame, Destination, Source, AIC_NET_ETHERTYPE_IPV4);
    UINT8 *Udp = Ip + AIC_NET_IPV4_HEADER_LENGTH;
    UINT32 IpTotal = AIC_NET_IPV4_HEADER_LENGTH + AIC_NET_UDP_HEADER_LENGTH + PayloadLength;
    UINT32 Index;

    Ip[0] = (UINT8)((AIC_NET_IPV4_VERSION << AIC_NET_IPV4_VERSION_SHIFT) | AIC_NET_IPV4_IHL_WORDS);
    Ip[1] = 0u; /* ToS：尽力而为 */
    AicNetPutBe16(Ip + AIC_NET_IPV4_TOTAL_LENGTH_OFFSET, (UINT16)IpTotal);
    AicNetPutBe16(Ip + 4u, 0u); /* identification：置 0，不分片 */
    AicNetPutBe16(Ip + 6u, AIC_NET_IPV4_FLAGS_DF);
    Ip[8] = AIC_NET_IPV4_TIME_TO_LIVE;
    Ip[9] = AIC_NET_IPV4_PROTOCOL_UDP;
    AicNetPutBe16(Ip + AIC_NET_IPV4_CHECKSUM_OFFSET, 0u); /* 先置 0 才能算校验和 */
    (void)memcpy(Ip + AIC_NET_IPV4_SOURCE_OFFSET, SourceIp, 4u);
    (void)memcpy(Ip + AIC_NET_IPV4_DESTINATION_OFFSET, DestinationIp, 4u);
    AicNetPutBe16(Ip + AIC_NET_IPV4_CHECKSUM_OFFSET,
                  AicNetChecksum(Ip, AIC_NET_IPV4_HEADER_LENGTH));
    AicNetPutBe16(Udp + AIC_NET_UDP_SOURCE_PORT_OFFSET, SourcePort);
    AicNetPutBe16(Udp + AIC_NET_UDP_DESTINATION_PORT_OFFSET, DestinationPort);
    AicNetPutBe16(Udp + AIC_NET_UDP_LENGTH_OFFSET,
                  (UINT16)(AIC_NET_UDP_HEADER_LENGTH + PayloadLength));
    AicNetPutBe16(Udp + AIC_NET_UDP_CHECKSUM_OFFSET, AIC_NET_UDP_CHECKSUM_NONE);
    for (Index = 0; Index < PayloadLength; Index++) {
        Udp[AIC_NET_UDP_HEADER_LENGTH + Index] = 0u;
    }
    return Udp + AIC_NET_UDP_HEADER_LENGTH;
}

/*
 * 收帧定位：从 ethertype 载荷（IP 头）走到 UDP 载荷。所有长度都按**报文自己声明的**
 * 值检查（IP total length、UDP length），越界就整帧不认 —— 广播帧后面可能被补齐到
 * 60 B，不能拿"收到的字节数"当 DHCP 报文长度。
 */
const UINT8 *AicNetFindUdpPayload(const UINT8 *Payload, UINT32 Length, UINT16 SourcePort,
                                  UINT16 DestinationPort, UINT32 *OutLength) {
    const UINT8 *Udp;
    UINT32 Ihl;
    UINT32 IpTotal;
    UINT32 UdpLength;

    if (Payload == 0 || Length < AIC_NET_IPV4_HEADER_LENGTH) {
        return 0;
    }
    if ((Payload[0] >> AIC_NET_IPV4_VERSION_SHIFT) != AIC_NET_IPV4_VERSION) {
        return 0;
    }
    Ihl = (UINT32)(Payload[0] & AIC_NET_IPV4_IHL_MASK) * 4u;
    if (Ihl < AIC_NET_IPV4_HEADER_LENGTH || Ihl > Length) {
        return 0;
    }
    if (Payload[AIC_NET_IPV4_PROTOCOL_OFFSET] != AIC_NET_IPV4_PROTOCOL_UDP) {
        return 0;
    }
    IpTotal = AicNetGetBe16(Payload + AIC_NET_IPV4_TOTAL_LENGTH_OFFSET);
    if (IpTotal < Ihl + AIC_NET_UDP_HEADER_LENGTH || IpTotal > Length) {
        return 0;
    }
    Udp = Payload + Ihl;
    if (AicNetGetBe16(Udp + AIC_NET_UDP_SOURCE_PORT_OFFSET) != SourcePort ||
        AicNetGetBe16(Udp + AIC_NET_UDP_DESTINATION_PORT_OFFSET) != DestinationPort) {
        return 0;
    }
    UdpLength = AicNetGetBe16(Udp + AIC_NET_UDP_LENGTH_OFFSET);
    if (UdpLength < AIC_NET_UDP_HEADER_LENGTH || UdpLength > IpTotal - Ihl) {
        return 0;
    }
    *OutLength = UdpLength - AIC_NET_UDP_HEADER_LENGTH;
    return Udp + AIC_NET_UDP_HEADER_LENGTH;
}

/*
 * 本机 STA MAC：优先 M1 的 addr1（AP 就是拿它算 PTK 的，所以它一定是空口上的那个），
 * 没握过手就退回 MM_ADD_IF 里写的 vif MAC。DHCP 的 chaddr/源 MAC、ARP 的 sha 都用它。
 */
int AicNetStationAddress(UINT8 *OutAddress) {
    const UINT8 *Station = AicHandshakeStationAddress();

    if (Station == 0) {
        Station = AicScanStationAddress();
    }
    if (Station == 0) {
        return -1;
    }
    (void)memcpy(OutAddress, Station, AIC_NET_MAC_LENGTH);
    return 0;
}

/*
 * 解密数据帧的分流口：WifiEapolParse.c 在解出 ethertype 之后调这里。
 * 两个处理函数各自看自己的 active 标志，不在跑的那条命令不打印、不解析。
 */
static void NetEtherSink(UINT16 EtherType, const UINT8 *Payload, UINT32 Length,
                         UINT32 Decryption) {
    if (EtherType == AIC_NET_ETHERTYPE_IPV4) {
        AicDhcpHandlePacket(Payload, Length, Decryption);
        return;
    }
    if (EtherType == AIC_NET_ETHERTYPE_ARP) {
        AicArpHandlePacket(Payload, Length, Decryption);
    }
}

void AicNetStart(void) {
    AicEapolSetEtherSink(NetEtherSink);
}

void AicNetStop(void) {
    AicEapolSetEtherSink(0);
}

/*
 * 预算内的收包等待：把预算交给 WifiEapol.c 的逐帧轮询（与 join/handshake 同一条路径），
 * Done 每到一帧问一次。每轮只给剩余预算，且**每轮清零计数器**：AicEapolPollRun 有
 * 256 帧的硬上限，收到一屏广播就把这一轮用完了 —— 清零后下一轮重新数，预算才是真闸门。
 */
int AicNetPollRun(SDHCI_HOST *Host, UINT32 BudgetMs, int (*Done)(void)) {
    static AIC_EAPOL_COUNTERS Counters;
    UINT64 Start = SdhciReadTime();
    UINT64 Budget = (UINT64)BudgetMs * AIC_NET_TIMEBASE_PER_MS;

    for (;;) {
        UINT64 Elapsed = SdhciReadTime() - Start;
        UINT64 Left;
        UINT32 LeftMs;

        if (Done != 0 && Done() != 0) {
            return 0;
        }
        if (Elapsed >= Budget) {
            return -1;
        }
        Left = Budget - Elapsed;
        LeftMs = (UINT32)(Left / AIC_NET_TIMEBASE_PER_MS);
        if (LeftMs == 0u) {
            LeftMs = 1u;
        }
        memset(&Counters, 0, sizeof(Counters));
        if (AicEapolPollRun(Host, LeftMs, &Counters, Done) != 0) {
            return -1;
        }
    }
}
