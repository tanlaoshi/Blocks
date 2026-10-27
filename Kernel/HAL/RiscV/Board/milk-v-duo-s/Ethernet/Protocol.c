#include "Ethernet/Private.h"

int DuoArpLookup(UINT32 Ip, UINT8 Mac[6]) {
    UINT32 Index;

    for (Index = 0; Index < ETHERNET_ARP_CACHE_SIZE; Index++) {
        if (gDuoArpCache[Index].Valid && gDuoArpCache[Index].Ip == Ip) {
            DuoMemoryCopy(Mac, gDuoArpCache[Index].Mac, 6);
            return 1;
        }
    }
    return 0;
}

void DuoArpLearn(UINT32 Ip, const UINT8 Mac[6]) {
    UINT32 Index;
    UINT32 Slot = 0;

    for (Index = 0; Index < ETHERNET_ARP_CACHE_SIZE; Index++) {
        if (gDuoArpCache[Index].Valid && gDuoArpCache[Index].Ip == Ip) {
            DuoMemoryCopy(gDuoArpCache[Index].Mac, Mac, 6);
            return;
        }
        if (!gDuoArpCache[Index].Valid) {
            Slot = Index;
            break;
        }
        Slot = Index;
    }
    gDuoArpCache[Slot].Ip = Ip;
    DuoMemoryCopy(gDuoArpCache[Slot].Mac, Mac, 6);
    gDuoArpCache[Slot].Valid = 1;
}

void DuoEthernetSendArpRequest(UINT32 TargetIp) {
    UINT8 Frame[ETHERNET_ETH_HEADER_LENGTH + sizeof(EthernetArpPacket)];
    EthernetEthHeader *Eth = (EthernetEthHeader *)Frame;
    EthernetArpPacket *Arp =
        (EthernetArpPacket *)(Frame + ETHERNET_ETH_HEADER_LENGTH);

    DuoMemorySet(Frame, 0, sizeof(Frame));
    DuoMemorySet(Eth->Destination, 0xFF, 6);
    DuoMemoryCopy(Eth->Source, gDuoMac, 6);
    Eth->EtherType = DuoByteSwap16(ETHERNET_ETH_TYPE_ARP);
    Arp->HardwareType = DuoByteSwap16(1);
    Arp->ProtocolType = DuoByteSwap16(ETHERNET_ETH_TYPE_IP);
    Arp->HardwareLength = 6;
    Arp->ProtocolLength = 4;
    Arp->Operation = DuoByteSwap16(ETHERNET_ARP_OPERATION_REQUEST);
    DuoMemoryCopy(Arp->SenderMac, gDuoMac, 6);
    Arp->SenderIp = DuoByteSwap32(gDuoIp);
    DuoMemorySet(Arp->TargetMac, 0, 6);
    Arp->TargetIp = DuoByteSwap32(TargetIp);
    (void)BoardEthernetSendFrame(Frame, sizeof(Frame));
}

void DuoEthernetHandleArp(const EthernetArpPacket *Arp) {
    UINT16 Operation = DuoByteSwap16(Arp->Operation);
    UINT32 SenderIp = DuoByteSwap32(Arp->SenderIp);

    if (Operation == ETHERNET_ARP_OPERATION_REPLY ||
        Operation == ETHERNET_ARP_OPERATION_REQUEST) {
        DuoArpLearn(SenderIp, Arp->SenderMac);
    }
    if (Operation == ETHERNET_ARP_OPERATION_REQUEST &&
        DuoByteSwap32(Arp->TargetIp) == gDuoIp) {
        UINT8 Frame[ETHERNET_ETH_HEADER_LENGTH + sizeof(EthernetArpPacket)];
        EthernetEthHeader *Eth = (EthernetEthHeader *)Frame;
        EthernetArpPacket *Reply =
            (EthernetArpPacket *)(Frame + ETHERNET_ETH_HEADER_LENGTH);

        DuoMemorySet(Frame, 0, sizeof(Frame));
        DuoMemoryCopy(Eth->Destination, Arp->SenderMac, 6);
        DuoMemoryCopy(Eth->Source, gDuoMac, 6);
        Eth->EtherType = DuoByteSwap16(ETHERNET_ETH_TYPE_ARP);
        Reply->HardwareType = DuoByteSwap16(1);
        Reply->ProtocolType = DuoByteSwap16(ETHERNET_ETH_TYPE_IP);
        Reply->HardwareLength = 6;
        Reply->ProtocolLength = 4;
        Reply->Operation = DuoByteSwap16(ETHERNET_ARP_OPERATION_REPLY);
        DuoMemoryCopy(Reply->SenderMac, gDuoMac, 6);
        Reply->SenderIp = DuoByteSwap32(gDuoIp);
        DuoMemoryCopy(Reply->TargetMac, Arp->SenderMac, 6);
        Reply->TargetIp = Arp->SenderIp;
        (void)BoardEthernetSendFrame(Frame, sizeof(Frame));
    }
}

void DuoEthernetHandleIcmp(const EthernetIpHeader *Ip, const UINT8 *Payload,
                               UINTN PayloadLength, const UINT8 *SourceMac) {
    const EthernetIcmpHeader *Icmp;
    UINT32 SourceIp;

    if (PayloadLength < ETHERNET_ICMP_HEADER_LENGTH) {
        return;
    }
    Icmp = (const EthernetIcmpHeader *)Payload;
    SourceIp = DuoByteSwap32(Ip->Source);
    if (Icmp->Type == ETHERNET_ICMP_ECHO_REPLY) {
        if (gDuoPingWait && SourceIp == gDuoPingTarget &&
            DuoByteSwap16(Icmp->Id) == gDuoPingId) {
            gDuoPingWait = 0;
        }
        return;
    }
    if (Icmp->Type == ETHERNET_ICMP_ECHO_REQUEST &&
        DuoByteSwap32(Ip->Destination) == gDuoIp && SourceMac != 0) {
        UINT8 Frame[ETHERNET_ETH_HEADER_LENGTH + ETHERNET_IP_HEADER_LENGTH +
                     1500u];
        EthernetEthHeader *Eth = (EthernetEthHeader *)Frame;
        EthernetIpHeader *OutIp =
            (EthernetIpHeader *)(Frame + ETHERNET_ETH_HEADER_LENGTH);
        EthernetIcmpHeader *OutIcmp =
            (EthernetIcmpHeader *)(Frame + ETHERNET_ETH_HEADER_LENGTH +
                                   ETHERNET_IP_HEADER_LENGTH);
        UINT16 Total = (UINT16)(ETHERNET_IP_HEADER_LENGTH + PayloadLength);
        UINT16 Sum;

        if (PayloadLength >
            sizeof(Frame) - ETHERNET_ETH_HEADER_LENGTH -
                ETHERNET_IP_HEADER_LENGTH) {
            return;
        }
        DuoMemorySet(Frame, 0, sizeof(Frame));
        DuoMemoryCopy(Eth->Destination, SourceMac, 6);
        DuoMemoryCopy(Eth->Source, gDuoMac, 6);
        Eth->EtherType = DuoByteSwap16(ETHERNET_ETH_TYPE_IP);
        OutIp->VersionIhl = 0x45u;
        OutIp->TimeToLive = 64u;
        OutIp->Protocol = ETHERNET_IP_PROTOCOL_ICMP;
        OutIp->Source = DuoByteSwap32(gDuoIp);
        OutIp->Destination = Ip->Source;
        OutIp->TotalLength = DuoByteSwap16(Total);
        OutIp->Identification = DuoByteSwap16(0x4F53u);
        OutIp->Checksum = 0;
        Sum = DuoEthernetChecksum(OutIp, ETHERNET_IP_HEADER_LENGTH);
        OutIp->Checksum = DuoByteSwap16(Sum);
        DuoMemoryCopy(OutIcmp, Payload, PayloadLength);
        OutIcmp->Type = ETHERNET_ICMP_ECHO_REPLY;
        OutIcmp->Checksum = 0;
        Sum = DuoEthernetChecksum(OutIcmp, PayloadLength);
        OutIcmp->Checksum = DuoByteSwap16(Sum);
        (void)BoardEthernetSendFrame(Frame, ETHERNET_ETH_HEADER_LENGTH + Total);
    }
}

void DuoEthernetHandleFrame(const UINT8 *Frame, UINTN FrameLength) {
    const EthernetEthHeader *Eth;
    UINT16 EtherType;

    if (Frame == 0 || FrameLength < ETHERNET_ETH_HEADER_LENGTH) {
        return;
    }
    /* dwmac 描述符长度常含 FCS；剥 4 再解析 */
    if (FrameLength >= ETHERNET_ETH_HEADER_LENGTH + 4u) {
        FrameLength -= 4u;
    }
#ifdef LWIP_ENABLE
    if (gDuoLwIpReceive) {
        LwIpNetifInput(Frame, FrameLength);
        return;
    }
#endif
    Eth = (const EthernetEthHeader *)Frame;
    EtherType = DuoByteSwap16(Eth->EtherType);
    if (EtherType == ETHERNET_ETH_TYPE_ARP) {
        if (FrameLength >=
            ETHERNET_ETH_HEADER_LENGTH + sizeof(EthernetArpPacket)) {
            DuoEthernetHandleArp(
                (const EthernetArpPacket *)(Frame + ETHERNET_ETH_HEADER_LENGTH));
        }
        return;
    }
    if (EtherType == ETHERNET_ETH_TYPE_IP) {
        const EthernetIpHeader *Ip;
        UINT16 Total;
        UINTN HeaderLength;
        UINTN PayloadLength;

        if (FrameLength <
            ETHERNET_ETH_HEADER_LENGTH + ETHERNET_IP_HEADER_LENGTH) {
            return;
        }
        Ip = (const EthernetIpHeader *)(Frame + ETHERNET_ETH_HEADER_LENGTH);
        HeaderLength = (UINTN)(Ip->VersionIhl & 0x0Fu) * 4u;
        if (HeaderLength < ETHERNET_IP_HEADER_LENGTH) {
            return;
        }
        Total = DuoByteSwap16(Ip->TotalLength);
        if (Total < HeaderLength) {
            return;
        }
        PayloadLength = (UINTN)Total - HeaderLength;
        if (ETHERNET_ETH_HEADER_LENGTH + HeaderLength + PayloadLength >
            FrameLength) {
            return;
        }
        if (Ip->Protocol == ETHERNET_IP_PROTOCOL_ICMP) {
            DuoEthernetHandleIcmp(Ip,
                               Frame + ETHERNET_ETH_HEADER_LENGTH + HeaderLength,
                               PayloadLength, Eth->Source);
        }
    }
}
