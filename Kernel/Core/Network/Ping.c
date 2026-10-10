/*
 * Ping.c — K38：ICMP echo（QEMU user 网关 10.0.2.2）
 *
 * 【初学者】ARP/IP 封帧见 Ip；本文件只组 ICMP 并等 Echo Reply。
 */
#include "Network.h"
#include "HalNet.h"

#if defined(__x86_64__) || defined(_M_X64)

#define ETH_HDR 14u
#define IP_HDR  20u
#define ICMP_HDR 8u
#define PING_PAY 32u
#define IP_ICMP 1u
#define ICMP_ECHO_REQ 8u
#define ICMP_ECHO_REP 0u

static UINT16 gPingId = 0x4F53u;
static UINT16 gPingSeq;

static UINT16 Sum16(const UINT8 *P, UINTN Len) {
    UINT32 Sum = 0;
    UINTN i;
    for (i = 0; i + 1 < Len; i += 2) {
        Sum += ((UINT32)P[i] << 8) | (UINT32)P[i + 1];
    }
    if (i < Len) {
        Sum += (UINT32)P[i] << 8;
    }
    while (Sum >> 16) {
        Sum = (Sum & 0xFFFFu) + (Sum >> 16);
    }
    return (UINT16)(~Sum);
}

static int MatchEchoReply(const UINT8 *Buf, int Len, UINT32 WantIp, UINT16 WantId,
                          UINT16 WantSeq) {
    UINT8 Ihl;
    UINT8 Proto;
    UINT32 SrcIp;
    UINT8 Type;
    UINT16 Id;
    UINT16 Seq;

    if (Len < (int)(ETH_HDR + IP_HDR + ICMP_HDR)) {
        return 0;
    }
    if (Buf[12] != 0x08 || Buf[13] != 0x00) {
        return 0;
    }
    Ihl = (UINT8)((Buf[ETH_HDR] & 0x0Fu) * 4u);
    if (Ihl < IP_HDR) {
        return 0;
    }
    Proto = Buf[ETH_HDR + 9];
    SrcIp = ((UINT32)Buf[ETH_HDR + 12] << 24) | ((UINT32)Buf[ETH_HDR + 13] << 16) |
            ((UINT32)Buf[ETH_HDR + 14] << 8) | (UINT32)Buf[ETH_HDR + 15];
    if (Proto != IP_ICMP || SrcIp != WantIp) {
        return 0;
    }
    if ((int)(ETH_HDR + Ihl + ICMP_HDR) > Len) {
        return 0;
    }
    Type = Buf[ETH_HDR + Ihl];
    Id = ((UINT16)Buf[ETH_HDR + Ihl + 4] << 8) | (UINT16)Buf[ETH_HDR + Ihl + 5];
    Seq = ((UINT16)Buf[ETH_HDR + Ihl + 6] << 8) | (UINT16)Buf[ETH_HDR + Ihl + 7];
    return (Type == ICMP_ECHO_REP && Id == WantId && Seq == WantSeq) ? 1 : 0;
}

int Ping(const char *Host, int TimeoutMs) {
    UINT32 Target;
    UINT8 Icmp[ICMP_HDR + PING_PAY];
    UINT8 Rx[1518];
    UINT16 Id;
    UINT16 Seq;
    UINTN i;
    int Spin;
    int RxLen;
    UINT32 Tries;

    if (!HalNetReady()) {
        return -1;
    }
    if (Host == 0 || Host[0] == 0) {
        Target = NetworkGwIp();
    } else if (NetworkParseIp(Host, &Target) != 0) {
        return -2;
    }

    gPingSeq++;
    Id = gPingId;
    Seq = gPingSeq;
    for (i = 0; i < sizeof(Icmp); i++) {
        Icmp[i] = 0;
    }
    Icmp[0] = ICMP_ECHO_REQ;
    Icmp[4] = (UINT8)(Id >> 8);
    Icmp[5] = (UINT8)(Id & 0xFFu);
    Icmp[6] = (UINT8)(Seq >> 8);
    Icmp[7] = (UINT8)(Seq & 0xFFu);
    for (i = 0; i < PING_PAY; i++) {
        Icmp[ICMP_HDR + i] = (UINT8)i;
    }
    {
        UINT16 Csum = Sum16(Icmp, sizeof(Icmp));
        Icmp[2] = (UINT8)(Csum >> 8);
        Icmp[3] = (UINT8)(Csum & 0xFFu);
    }

    if (NetworkSendIp(Target, IP_ICMP, Icmp, sizeof(Icmp)) != 0) {
        return -4;
    }

    Tries = (TimeoutMs > 0) ? (UINT32)(TimeoutMs / 2) : 1500u;
    for (Spin = 0; (UINT32)Spin < Tries; Spin++) {
        RxLen = HalNetReceive(Rx, sizeof(Rx));
        if (RxLen > 0) {
            NetworkEatArpFromFrame(Rx, RxLen);
            UdpInputFrame(Rx, RxLen); /* 顺手喂 UDP 队列 */
            if (MatchEchoReply(Rx, RxLen, Target, Id, Seq)) {
                return 0;
            }
        }
        __asm__ volatile("pause");
    }
    return -5;
}

#else

int Ping(const char *Host, int TimeoutMs) {
    (void)Host;
    (void)TimeoutMs;
    return -1;
}

#endif
