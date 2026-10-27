#include "Ethernet/Private.h"

int DuoEthernetReady(void) {
    return gDuoBound && gDuoDmaOk && DuoPhyLinkUp();
}

void DuoEthernetGetMac(UINT8 Mac[6]) {
    if (Mac) {
        DuoMemoryCopy(Mac, gDuoMac, 6);
    }
}

UINT32 DuoEthernetGetIp(void) {
    return gDuoIp;
}

void DuoEthernetSetIp(UINT32 Ip) {
    gDuoIp = Ip;
}

void DuoEthernetFormatIp(UINT32 Ip, char *Buffer, int BufferLength) {
    if (!Buffer || BufferLength < 16) {
        return;
    }
    FormatDottedDecimal(Ip, Buffer, BufferLength);
}

int DuoEthernetParseIp(const char *Text, UINT32 *Ip) {
    UINT32 Parts[4];
    int Part = 0;
    UINT32 Value = 0;
    int Digits = 0;

    if (Text == 0 || Ip == 0) {
        return -1;
    }
    while (*Text) {
        if (*Text >= '0' && *Text <= '9') {
            Value = Value * 10u + (UINT32)(*Text - '0');
            if (Value > 255u) {
                return -1;
            }
            Digits++;
        } else if (*Text == '.') {
            if (Digits == 0 || Part >= 3) {
                return -1;
            }
            Parts[Part++] = Value;
            Value = 0;
            Digits = 0;
        } else {
            return -1;
        }
        Text++;
    }
    if (Digits == 0 || Part != 3) {
        return -1;
    }
    Parts[3] = Value;
    *Ip = (Parts[0] << 24) | (Parts[1] << 16) | (Parts[2] << 8) | Parts[3];
    return 0;
}

UINT16 DuoEthernetChecksum(const void *Data, UINTN Length) {
    const UINT8 *Bytes = (const UINT8 *)Data;
    UINT32 Sum = 0;
    UINTN Index;

    for (Index = 0; Index + 1u < Length; Index += 2u) {
        Sum += ((UINT32)Bytes[Index] << 8) | Bytes[Index + 1u];
    }
    if (Index < Length) {
        Sum += (UINT32)Bytes[Index] << 8;
    }
    while (Sum >> 16) {
        Sum = (Sum & 0xFFFFu) + (Sum >> 16);
    }
    return (UINT16)~Sum;
}

void DuoEthernetGetStats(UINT32 *TransmitDone, UINT32 *ReceiveFrames) {
    if (TransmitDone) {
        *TransmitDone = gDuoTransmitDone;
    }
    if (ReceiveFrames) {
        *ReceiveFrames = gDuoReceiveFrames;
    }
}

void DuoEthernetSetLwIpReceive(int Enable) {
    gDuoLwIpReceive = Enable ? 1 : 0;
}

/*
 * net-5：SendIp / Ping 经 ARP；对齐 Virtio Protocol.c。
 */
int DuoEthernetSendIp(UINT32 DestinationIp, UINT8 Protocol,
                          const void *Payload, UINTN PayloadLength) {
    UINT8 DestinationMac[6];
    UINT8 Frame[ETHERNET_ETH_HEADER_LENGTH + ETHERNET_IP_HEADER_LENGTH + 1400u];
    EthernetEthHeader *Eth;
    EthernetIpHeader *Ip;
    UINT16 Total;
    UINT16 Sum;

    if (!DuoEthernetReady() || gDuoLwIpReceive || Payload == 0 ||
        PayloadLength > 1400u) {
        return -1;
    }
    if (DuoEthernetResolve(DestinationIp, DestinationMac, 2000) != 0) {
        return -2;
    }
    Total = (UINT16)(ETHERNET_IP_HEADER_LENGTH + PayloadLength);
    DuoMemorySet(Frame, 0, sizeof(Frame));
    Eth = (EthernetEthHeader *)Frame;
    DuoMemoryCopy(Eth->Destination, DestinationMac, 6);
    DuoMemoryCopy(Eth->Source, gDuoMac, 6);
    Eth->EtherType = DuoByteSwap16(ETHERNET_ETH_TYPE_IP);
    Ip = (EthernetIpHeader *)(Frame + ETHERNET_ETH_HEADER_LENGTH);
    Ip->VersionIhl = 0x45u;
    Ip->TimeToLive = 64u;
    Ip->Protocol = Protocol;
    Ip->Source = DuoByteSwap32(gDuoIp);
    Ip->Destination = DuoByteSwap32(DestinationIp);
    Ip->TotalLength = DuoByteSwap16(Total);
    Ip->Identification = DuoByteSwap16(0x4F53u);
    Ip->Checksum = 0;
    Sum = DuoEthernetChecksum(Ip, ETHERNET_IP_HEADER_LENGTH);
    Ip->Checksum = DuoByteSwap16(Sum);
    DuoMemoryCopy(Frame + ETHERNET_ETH_HEADER_LENGTH + ETHERNET_IP_HEADER_LENGTH,
               Payload, PayloadLength);
    return BoardEthernetSendFrame(Frame, ETHERNET_ETH_HEADER_LENGTH + Total);
}

int DuoEthernetPing(const char *Host, int TimeoutMilliseconds) {
    UINT32 Target;
    UINT8 DestinationMac[6];
    UINT8 Frame[ETHERNET_ETH_HEADER_LENGTH + ETHERNET_IP_HEADER_LENGTH +
                ETHERNET_ICMP_HEADER_LENGTH + ETHERNET_PING_PAYLOAD_LENGTH];
    EthernetEthHeader *Eth;
    EthernetIpHeader *Ip;
    EthernetIcmpHeader *Icmp;
    UINT8 *Payload;
    UINT16 Total;
    UINT16 Sum;
    int Tries;
    UINTN Index;

    if (!DuoEthernetReady() || gDuoLwIpReceive) {
        return -1;
    }
    if (DuoEthernetParseIp(Host, &Target) != 0) {
        return -1;
    }
    if (DuoEthernetResolve(Target, DestinationMac, TimeoutMilliseconds) != 0) {
        return -1;
    }
    DuoMemorySet(Frame, 0, sizeof(Frame));
    Eth = (EthernetEthHeader *)Frame;
    DuoMemoryCopy(Eth->Destination, DestinationMac, 6);
    DuoMemoryCopy(Eth->Source, gDuoMac, 6);
    Eth->EtherType = DuoByteSwap16(ETHERNET_ETH_TYPE_IP);
    Ip = (EthernetIpHeader *)(Frame + ETHERNET_ETH_HEADER_LENGTH);
    Ip->VersionIhl = 0x45u;
    Ip->TimeToLive = 64u;
    Ip->Protocol = ETHERNET_IP_PROTOCOL_ICMP;
    Ip->Source = DuoByteSwap32(gDuoIp);
    Ip->Destination = DuoByteSwap32(Target);
    Total = ETHERNET_IP_HEADER_LENGTH + ETHERNET_ICMP_HEADER_LENGTH +
            ETHERNET_PING_PAYLOAD_LENGTH;
    Ip->TotalLength = DuoByteSwap16(Total);
    Ip->Identification = DuoByteSwap16(0x1234u);
    Ip->Checksum = 0;
    Sum = DuoEthernetChecksum(Ip, ETHERNET_IP_HEADER_LENGTH);
    Ip->Checksum = DuoByteSwap16(Sum);
    Icmp = (EthernetIcmpHeader *)(Frame + ETHERNET_ETH_HEADER_LENGTH +
                                  ETHERNET_IP_HEADER_LENGTH);
    gDuoPingSequence++;
    Icmp->Type = ETHERNET_ICMP_ECHO_REQUEST;
    Icmp->Code = 0;
    Icmp->Id = DuoByteSwap16(gDuoPingId);
    Icmp->Sequence = DuoByteSwap16(gDuoPingSequence);
    Payload = Frame + ETHERNET_ETH_HEADER_LENGTH + ETHERNET_IP_HEADER_LENGTH +
              ETHERNET_ICMP_HEADER_LENGTH;
    for (Index = 0; Index < ETHERNET_PING_PAYLOAD_LENGTH; Index++) {
        Payload[Index] = (UINT8)Index;
    }
    Icmp->Checksum = 0;
    Sum = DuoEthernetChecksum(Icmp, ETHERNET_ICMP_HEADER_LENGTH +
                                     ETHERNET_PING_PAYLOAD_LENGTH);
    Icmp->Checksum = DuoByteSwap16(Sum);
    gDuoPingTarget = Target;
    gDuoPingWait = 1;
    if (BoardEthernetSendFrame(Frame, ETHERNET_ETH_HEADER_LENGTH + Total) != 0) {
        gDuoPingWait = 0;
        return -2;
    }
    Tries = TimeoutMilliseconds > 0 ? TimeoutMilliseconds / 10 : 300;
    while (Tries-- > 0 && gDuoPingWait) {
        DuoEthernetPoll();
        HalCpuHalt();
    }
    if (gDuoPingWait) {
        gDuoPingWait = 0;
        return -3;
    }
    return 0;
}
