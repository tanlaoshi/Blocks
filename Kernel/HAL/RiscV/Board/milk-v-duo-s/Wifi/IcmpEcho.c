/*
 * WifiIcmpEcho.c — 单播 ICMP Echo（绕过 ARP，以太网 DA=BSSID）
 *
 * 动机（Docs/20 §1g）：空口离线 CCMP 已证伪「错 TK / TX MIC」；AP 仍不回 ARP。
 * 本探针发 Echo 到网关 IP，DA 直接填 AP BSSID —— 有回则 ARP 特例；无回则 AP 对
 * 单播 L3 一律沉默。判据标签：unicast-icmp-ok | unicast-icmp-fail。
 */
#include "Wifi/NetPrivate.h"
#include "CString.h"

#define AIC_NET_IPV4_PROTOCOL_ICMP 1u /* IANA ICMP */
#define AIC_NET_ICMP_HEADER_LENGTH 8u
#define AIC_NET_ICMP_TYPE_ECHO_REPLY 0u
#define AIC_NET_ICMP_TYPE_ECHO_REQUEST 8u
#define AIC_NET_ICMP_CODE_NONE 0u
#define AIC_NET_ICMP_TYPE_OFFSET 0u
#define AIC_NET_ICMP_CODE_OFFSET 1u
#define AIC_NET_ICMP_CHECKSUM_OFFSET 2u
#define AIC_NET_ICMP_IDENTIFIER_OFFSET 4u
#define AIC_NET_ICMP_SEQUENCE_OFFSET 6u
#define AIC_NET_ICMP_ECHO_PAYLOAD_LENGTH 16u
#define AIC_NET_ICMP_ECHO_TRIES 2u
#define AIC_NET_ICMP_ECHO_IDENTIFIER 0x424Bu /* 'BK' */

static UINT8 sAicIcmpFrame[AIC_NET_FRAME_MAX];
static UINT8 sAicIcmpTargetIp[AIC_NET_IPV4_LENGTH];
static UINT16 sAicIcmpSequence;
static UINT32 sAicIcmpActive;
static UINT32 sAicIcmpReplySeen;

static UINT32 IcmpEchoBuild(UINT8 *Frame, UINT32 Capacity, const UINT8 *DestinationMac,
                            const UINT8 *Station, const UINT8 *StationIp,
                            const UINT8 *TargetIp, UINT16 Sequence) {
    UINT8 *Ip;
    UINT8 *Icmp;
    UINT32 IpTotal;
    UINT32 Index;
    UINT32 Length;

    if (Frame == 0 || DestinationMac == 0 || Station == 0 || StationIp == 0 || TargetIp == 0) {
        return 0u;
    }
    Length = AIC_NET_ETHERNET_HEADER_LENGTH + AIC_NET_IPV4_HEADER_LENGTH +
             AIC_NET_ICMP_HEADER_LENGTH + AIC_NET_ICMP_ECHO_PAYLOAD_LENGTH;
    if (Capacity < Length) {
        return 0u;
    }
    Ip = AicNetAddEthernetHeader(Frame, DestinationMac, Station, AIC_NET_ETHERTYPE_IPV4);
    IpTotal = AIC_NET_IPV4_HEADER_LENGTH + AIC_NET_ICMP_HEADER_LENGTH +
              AIC_NET_ICMP_ECHO_PAYLOAD_LENGTH;
    Ip[0] = (UINT8)((AIC_NET_IPV4_VERSION << AIC_NET_IPV4_VERSION_SHIFT) | AIC_NET_IPV4_IHL_WORDS);
    Ip[1] = 0u;
    AicNetPutBe16(Ip + AIC_NET_IPV4_TOTAL_LENGTH_OFFSET, (UINT16)IpTotal);
    AicNetPutBe16(Ip + 4u, Sequence); /* identification：用序号，便于空口对表 */
    AicNetPutBe16(Ip + 6u, AIC_NET_IPV4_FLAGS_DF);
    Ip[8] = AIC_NET_IPV4_TIME_TO_LIVE;
    Ip[9] = (UINT8)AIC_NET_IPV4_PROTOCOL_ICMP;
    AicNetPutBe16(Ip + AIC_NET_IPV4_CHECKSUM_OFFSET, 0u);
    (void)memcpy(Ip + AIC_NET_IPV4_SOURCE_OFFSET, StationIp, AIC_NET_IPV4_LENGTH);
    (void)memcpy(Ip + AIC_NET_IPV4_DESTINATION_OFFSET, TargetIp, AIC_NET_IPV4_LENGTH);
    AicNetPutBe16(Ip + AIC_NET_IPV4_CHECKSUM_OFFSET,
                  AicNetChecksum(Ip, AIC_NET_IPV4_HEADER_LENGTH));

    Icmp = Ip + AIC_NET_IPV4_HEADER_LENGTH;
    Icmp[AIC_NET_ICMP_TYPE_OFFSET] = (UINT8)AIC_NET_ICMP_TYPE_ECHO_REQUEST;
    Icmp[AIC_NET_ICMP_CODE_OFFSET] = (UINT8)AIC_NET_ICMP_CODE_NONE;
    AicNetPutBe16(Icmp + AIC_NET_ICMP_CHECKSUM_OFFSET, 0u);
    AicNetPutBe16(Icmp + AIC_NET_ICMP_IDENTIFIER_OFFSET, (UINT16)AIC_NET_ICMP_ECHO_IDENTIFIER);
    AicNetPutBe16(Icmp + AIC_NET_ICMP_SEQUENCE_OFFSET, Sequence);
    for (Index = 0; Index < AIC_NET_ICMP_ECHO_PAYLOAD_LENGTH; Index++) {
        Icmp[AIC_NET_ICMP_HEADER_LENGTH + Index] = (UINT8)(0x40u + Index);
    }
    AicNetPutBe16(Icmp + AIC_NET_ICMP_CHECKSUM_OFFSET,
                  AicNetChecksum(Icmp, AIC_NET_ICMP_HEADER_LENGTH +
                                           AIC_NET_ICMP_ECHO_PAYLOAD_LENGTH));
    return Length;
}

void AicIcmpHandlePacket(const UINT8 *Payload, UINT32 Length, UINT32 Decryption) {
    const UINT8 *Icmp;
    UINT8 VersionIhl;
    UINT8 HeaderLength;
    UINT16 TotalLength;
    UINT16 Identifier;
    UINT16 Sequence;

    (void)Decryption;
    if (sAicIcmpActive == 0u || sAicIcmpReplySeen != 0u || Payload == 0) {
        return;
    }
    if (Length < AIC_NET_IPV4_HEADER_LENGTH + AIC_NET_ICMP_HEADER_LENGTH) {
        return;
    }
    VersionIhl = Payload[0];
    if ((VersionIhl >> AIC_NET_IPV4_VERSION_SHIFT) != AIC_NET_IPV4_VERSION) {
        return;
    }
    HeaderLength = (UINT8)((VersionIhl & AIC_NET_IPV4_IHL_MASK) * 4u);
    if (HeaderLength < AIC_NET_IPV4_HEADER_LENGTH || Length < HeaderLength + AIC_NET_ICMP_HEADER_LENGTH) {
        return;
    }
    if (Payload[AIC_NET_IPV4_PROTOCOL_OFFSET] != (UINT8)AIC_NET_IPV4_PROTOCOL_ICMP) {
        return;
    }
    TotalLength = AicNetGetBe16(Payload + AIC_NET_IPV4_TOTAL_LENGTH_OFFSET);
    if (TotalLength < HeaderLength + AIC_NET_ICMP_HEADER_LENGTH || TotalLength > Length) {
        return;
    }
    if (memcmp(Payload + AIC_NET_IPV4_SOURCE_OFFSET, sAicIcmpTargetIp, AIC_NET_IPV4_LENGTH) != 0) {
        return;
    }
    Icmp = Payload + HeaderLength;
    if (Icmp[AIC_NET_ICMP_TYPE_OFFSET] != (UINT8)AIC_NET_ICMP_TYPE_ECHO_REPLY ||
        Icmp[AIC_NET_ICMP_CODE_OFFSET] != (UINT8)AIC_NET_ICMP_CODE_NONE) {
        return;
    }
    Identifier = AicNetGetBe16(Icmp + AIC_NET_ICMP_IDENTIFIER_OFFSET);
    Sequence = AicNetGetBe16(Icmp + AIC_NET_ICMP_SEQUENCE_OFFSET);
    if (Identifier != (UINT16)AIC_NET_ICMP_ECHO_IDENTIFIER || Sequence != sAicIcmpSequence) {
        return;
    }
    sAicIcmpReplySeen = 1u;
}

static int IcmpReplyDone(void) {
    return (int)sAicIcmpReplySeen;
}

int AicIcmpEchoUnicast(SDHCI_HOST *Host, const UINT8 *Station, const UINT8 *StationIp,
                       const UINT8 *TargetIp, const UINT8 *DestinationMac, UINT8 VifIndex,
                       UINT8 ApIndex, UINT32 BudgetMilliseconds) {
    UINT32 Length;
    UINT32 Try;
    UINT32 Slice;
    char TargetText[AIC_NET_IPV4_TEXT_MAX];

    if (Host == 0 || Station == 0 || StationIp == 0 || TargetIp == 0 || DestinationMac == 0) {
        return -1;
    }
    (void)memcpy(sAicIcmpTargetIp, TargetIp, AIC_NET_IPV4_LENGTH);
    sAicIcmpReplySeen = 0u;
    sAicIcmpActive = 1u;
    AicNetFormatIpv4(TargetIp, TargetText);
    Slice = BudgetMilliseconds / AIC_NET_ICMP_ECHO_TRIES;
    if (Slice == 0u) {
        Slice = 1u;
    }
    for (Try = 0; Try < AIC_NET_ICMP_ECHO_TRIES; Try++) {
        sAicIcmpSequence = (UINT16)(Try + 1u);
        Length = IcmpEchoBuild(sAicIcmpFrame, (UINT32)sizeof(sAicIcmpFrame), DestinationMac,
                               Station, StationIp, TargetIp, sAicIcmpSequence);
        if (Length == 0u) {
            sAicIcmpActive = 0u;
            SdhciMark("WiFi: icmp uni: build failed\n");
            return -1;
        }
        SdhciMark("WiFi: icmp: echo-request ");
        SdhciMark(TargetText);
        SdhciMark("\n");
        if (AicWifiDataSend(Host, sAicIcmpFrame, Length, VifIndex, ApIndex) != 0) {
            sAicIcmpActive = 0u;
            SdhciMark("WiFi: icmp: tx failed → unicast-icmp-fail\n");
            return -1;
        }
        if (AicNetPollRun(Host, Slice, IcmpReplyDone) == 0) {
            break;
        }
    }
    sAicIcmpActive = 0u;
    if (sAicIcmpReplySeen == 0u) {
        SdhciMark("WiFi: icmp: no reply → unicast-icmp-fail\n");
        return -1;
    }
    SdhciMark("WiFi: icmp: reply ok → unicast-icmp-ok\n");
    return 0;
}
