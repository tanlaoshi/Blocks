/*
 * NetFrame.c — DHCP/ARP 共用的帧公共层（字节序、校验和、IP/UDP 头）
 *
 * 收包分流/轮询见 NetFramePoll.c。
 * 别改：网络字节序由 AicNetPutBe16/32 显式写；不能用结构体赋值（RISC-V 小端）。
 * 来源：RFC 791 §3.1、RFC 768、RFC 894 §2、RFC 2131 §2、RFC 826。
 */
#include "Wifi/NetPrivate.h"
#include "CString.h"
#include "Compiler.h" /* BLOCKS_OFFSETOF */

const UINT8 gAicNetBroadcastMac[AIC_NET_MAC_LENGTH] = {
    0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu};
const UINT8 gAicNetBroadcastIp[AIC_NET_IPV4_LENGTH] = {0xFFu, 0xFFu, 0xFFu, 0xFFu};
const UINT8 gAicNetZeroIp[AIC_NET_IPV4_LENGTH] = {0u, 0u, 0u, 0u};

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
    {"dhcp.chaddr", (UINT32)sizeof(((AIC_NET_DHCP_FIXED *)0)->ClientHardware),
     AIC_NET_DHCP_CLIENT_HARDWARE_LENGTH},
    {"arp", (UINT32)sizeof(AIC_NET_ARP_PACKET), AIC_NET_ARP_PACKET_LENGTH},
    {"arp.sender_hw", (UINT32)BLOCKS_OFFSETOF(AIC_NET_ARP_PACKET, SenderHardware),
     AIC_NET_ARP_SENDER_HARDWARE_OFFSET},
    {"arp.target_proto", (UINT32)BLOCKS_OFFSETOF(AIC_NET_ARP_PACKET, TargetProtocol),
     AIC_NET_ARP_TARGET_PROTOCOL_OFFSET},
};

#define AIC_NET_LAYOUT_COUNT ((UINT32)(sizeof(sAicNetLayout) / sizeof(sAicNetLayout[0])))

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

UINT8 *AicNetAddEthernetHeader(UINT8 *Frame, const UINT8 *Destination, const UINT8 *Source,
                               UINT16 EtherType) {
    (void)memcpy(Frame, Destination, AIC_NET_MAC_LENGTH);
    (void)memcpy(Frame + AIC_NET_MAC_LENGTH, Source, AIC_NET_MAC_LENGTH);
    AicNetPutBe16(Frame + AIC_NET_ETHERTYPE_OFFSET, EtherType);
    return Frame + AIC_NET_ETHERNET_HEADER_LENGTH;
}

UINT8 *AicNetBuildIpv4UdpFrame(UINT8 *Frame, const UINT8 *Destination, const UINT8 *Source,
                               const UINT8 *SourceIp, const UINT8 *DestinationIp,
                               UINT16 SourcePort, UINT16 DestinationPort,
                               UINT32 PayloadLength) {
    UINT8 *Ip = AicNetAddEthernetHeader(Frame, Destination, Source, AIC_NET_ETHERTYPE_IPV4);
    UINT8 *Udp = Ip + AIC_NET_IPV4_HEADER_LENGTH;
    UINT32 IpTotal = AIC_NET_IPV4_HEADER_LENGTH + AIC_NET_UDP_HEADER_LENGTH + PayloadLength;
    UINT32 Index;

    Ip[0] = (UINT8)((AIC_NET_IPV4_VERSION << AIC_NET_IPV4_VERSION_SHIFT) | AIC_NET_IPV4_IHL_WORDS);
    Ip[1] = 0u;
    AicNetPutBe16(Ip + AIC_NET_IPV4_TOTAL_LENGTH_OFFSET, (UINT16)IpTotal);
    AicNetPutBe16(Ip + 4u, 0u);
    AicNetPutBe16(Ip + 6u, AIC_NET_IPV4_FLAGS_DF);
    Ip[8] = AIC_NET_IPV4_TIME_TO_LIVE;
    Ip[9] = AIC_NET_IPV4_PROTOCOL_UDP;
    AicNetPutBe16(Ip + AIC_NET_IPV4_CHECKSUM_OFFSET, 0u);
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

const UINT8 *AicNetFindUdpPayload(const UINT8 *Payload, UINT32 Length, UINT16 SourcePort,
                                  UINT16 DestinationPort, UINT32 *OutLength) {
    const UINT8 *Udp;
    UINT32 InternetHeaderLength;
    UINT32 IpTotal;
    UINT32 UdpLength;

    if (Payload == 0 || Length < AIC_NET_IPV4_HEADER_LENGTH) {
        return 0;
    }
    if ((Payload[0] >> AIC_NET_IPV4_VERSION_SHIFT) != AIC_NET_IPV4_VERSION) {
        return 0;
    }
    InternetHeaderLength = (UINT32)(Payload[0] & AIC_NET_IPV4_IHL_MASK) * 4u;
    if (InternetHeaderLength < AIC_NET_IPV4_HEADER_LENGTH || InternetHeaderLength > Length) {
        return 0;
    }
    if (Payload[AIC_NET_IPV4_PROTOCOL_OFFSET] != AIC_NET_IPV4_PROTOCOL_UDP) {
        return 0;
    }
    IpTotal = AicNetGetBe16(Payload + AIC_NET_IPV4_TOTAL_LENGTH_OFFSET);
    if (IpTotal < InternetHeaderLength + AIC_NET_UDP_HEADER_LENGTH || IpTotal > Length) {
        return 0;
    }
    Udp = Payload + InternetHeaderLength;
    if (AicNetGetBe16(Udp + AIC_NET_UDP_SOURCE_PORT_OFFSET) != SourcePort ||
        AicNetGetBe16(Udp + AIC_NET_UDP_DESTINATION_PORT_OFFSET) != DestinationPort) {
        return 0;
    }
    UdpLength = AicNetGetBe16(Udp + AIC_NET_UDP_LENGTH_OFFSET);
    if (UdpLength < AIC_NET_UDP_HEADER_LENGTH || UdpLength > IpTotal - InternetHeaderLength) {
        return 0;
    }
    *OutLength = UdpLength - AIC_NET_UDP_HEADER_LENGTH;
    return Udp + AIC_NET_UDP_HEADER_LENGTH;
}
