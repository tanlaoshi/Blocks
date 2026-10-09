/*
 * NetworkPing.c — K38：ICMP echo（QEMU user 网关 10.0.2.2）
 *
 * 【初学者】
 * 走 HalNet 裸以太网帧：先 ARP 解析目标 MAC，再发 ICMP Echo Request，
 * 轮询 Echo Reply。不做 lwIP / DNS。默认本机 10.0.2.15。
 */
#include "Network.h"
#include "HalNet.h"
#include "HalSerial.h"
#include "ToySerialConfig.h"

#if defined(__x86_64__) || defined(_M_X64)

#define ETH_HDR 14u
#define IP_HDR  20u
#define ICMP_HDR 8u
#define PING_PAY 32u
#define ARP_PKT 28u

#define ETH_ARP 0x0806u
#define ETH_IP  0x0800u
#define IP_ICMP 1u
#define ARP_REQ 1u
#define ARP_REP 2u
#define ICMP_ECHO_REQ 8u
#define ICMP_ECHO_REP 0u

#define IP_SELF 0x0A00020Fu /* 10.0.2.15 */
#define IP_GW   0x0A000202u /* 10.0.2.2 */

static UINT16 gPingId = 0x4F53u;
static UINT16 gPingSeq;
static UINT8 gGwMac[6];
static int gGwMacOk;

static void Copy(void *D, const void *S, UINTN N) {
    UINT8 *A = (UINT8 *)D;
    const UINT8 *B = (const UINT8 *)S;
    UINTN i;
    for (i = 0; i < N; i++) {
        A[i] = B[i];
    }
}

static void Zero(void *P, UINTN N) {
    UINT8 *B = (UINT8 *)P;
    UINTN i;
    for (i = 0; i < N; i++) {
        B[i] = 0;
    }
}

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

static int ParseDec(const char **Pp, UINT32 *Out) {
    const char *P = *Pp;
    UINT32 V = 0;
    int Dig = 0;
    while (*P >= '0' && *P <= '9') {
        V = V * 10u + (UINT32)(*P - '0');
        if (V > 255u) {
            return -1;
        }
        P++;
        Dig = 1;
    }
    if (!Dig) {
        return -1;
    }
    *Out = V;
    *Pp = P;
    return 0;
}

int NetworkParseIp(const char *S, UINT32 *Out) {
    const char *P = S;
    UINT32 A, B, C, D;
    if (S == 0 || Out == 0) {
        return -1;
    }
    if (ParseDec(&P, &A) != 0 || *P++ != '.') {
        return -1;
    }
    if (ParseDec(&P, &B) != 0 || *P++ != '.') {
        return -1;
    }
    if (ParseDec(&P, &C) != 0 || *P++ != '.') {
        return -1;
    }
    if (ParseDec(&P, &D) != 0 || *P != 0) {
        return -1;
    }
    *Out = (A << 24) | (B << 16) | (C << 8) | D;
    return 0;
}

static void LearnArp(UINT32 Ip, const UINT8 Mac[6]) {
    if (Ip == IP_GW || Ip == 0) {
        Copy(gGwMac, Mac, 6);
        gGwMacOk = 1;
    }
}

static int ProcessRx(UINT8 *Buf, int Len, UINT32 WantIp, UINT16 WantId,
                     UINT16 WantSeq, int *GotReply) {
    UINT16 EthType;
    UINT32 Spa;
    UINT16 Op;
    UINT8 Ihl;
    UINT16 Tot;
    UINT8 Proto;
    UINT32 SrcIp;
    UINT8 Type;
    UINT16 Id;
    UINT16 Seq;

    if (Len < (int)ETH_HDR) {
        return 0;
    }
    EthType = ((UINT16)Buf[12] << 8) | (UINT16)Buf[13];
    if (EthType == ETH_ARP && Len >= (int)(ETH_HDR + ARP_PKT)) {
        Op = ((UINT16)Buf[ETH_HDR + 6] << 8) | (UINT16)Buf[ETH_HDR + 7];
        Spa = ((UINT32)Buf[ETH_HDR + 14] << 24) | ((UINT32)Buf[ETH_HDR + 15] << 16) |
              ((UINT32)Buf[ETH_HDR + 16] << 8) | (UINT32)Buf[ETH_HDR + 17];
        if (Op == ARP_REP || Op == ARP_REQ) {
            LearnArp(Spa, Buf + ETH_HDR + 8);
        }
        return 0;
    }
    if (EthType != ETH_IP || Len < (int)(ETH_HDR + IP_HDR + ICMP_HDR)) {
        return 0;
    }
    Ihl = (UINT8)((Buf[ETH_HDR] & 0x0Fu) * 4u);
    if (Ihl < IP_HDR) {
        return 0;
    }
    Tot = ((UINT16)Buf[ETH_HDR + 2] << 8) | (UINT16)Buf[ETH_HDR + 3];
    Proto = Buf[ETH_HDR + 9];
    SrcIp = ((UINT32)Buf[ETH_HDR + 12] << 24) | ((UINT32)Buf[ETH_HDR + 13] << 16) |
            ((UINT32)Buf[ETH_HDR + 14] << 8) | (UINT32)Buf[ETH_HDR + 15];
    if (Proto != IP_ICMP || SrcIp != WantIp) {
        return 0;
    }
    if ((int)(ETH_HDR + Ihl + ICMP_HDR) > Len) {
        return 0;
    }
    (void)Tot;
    Type = Buf[ETH_HDR + Ihl];
    Id = ((UINT16)Buf[ETH_HDR + Ihl + 4] << 8) | (UINT16)Buf[ETH_HDR + Ihl + 5];
    Seq = ((UINT16)Buf[ETH_HDR + Ihl + 6] << 8) | (UINT16)Buf[ETH_HDR + Ihl + 7];
    if (Type == ICMP_ECHO_REP && Id == WantId && Seq == WantSeq) {
        *GotReply = 1;
        return 1;
    }
    return 0;
}

static int ArpResolve(UINT32 TargetIp, UINT8 Mac[6], int TimeoutMs) {
    UINT8 Frame[ETH_HDR + ARP_PKT];
    UINT8 MyMac[6];
    UINT8 Rx[1518];
    int Spin;
    int RxLen;
    UINT32 Tries;

    if (TargetIp == IP_GW && gGwMacOk) {
        Copy(Mac, gGwMac, 6);
        return 0;
    }

    HalNetGetMac(MyMac);
    Zero(Frame, sizeof(Frame));
    {
        UINTN i;
        for (i = 0; i < 6; i++) {
            Frame[i] = 0xFF;
            Frame[6 + i] = MyMac[i];
        }
    }
    Frame[12] = 0x08;
    Frame[13] = 0x06;
    Frame[14] = 0x00;
    Frame[15] = 0x01;
    Frame[16] = 0x08;
    Frame[17] = 0x00;
    Frame[18] = 6;
    Frame[19] = 4;
    Frame[20] = 0x00;
    Frame[21] = 0x01; /* request */
    Copy(Frame + 22, MyMac, 6);
    Frame[28] = (UINT8)((IP_SELF >> 24) & 0xFFu);
    Frame[29] = (UINT8)((IP_SELF >> 16) & 0xFFu);
    Frame[30] = (UINT8)((IP_SELF >> 8) & 0xFFu);
    Frame[31] = (UINT8)(IP_SELF & 0xFFu);
    Frame[38] = (UINT8)((TargetIp >> 24) & 0xFFu);
    Frame[39] = (UINT8)((TargetIp >> 16) & 0xFFu);
    Frame[40] = (UINT8)((TargetIp >> 8) & 0xFFu);
    Frame[41] = (UINT8)(TargetIp & 0xFFu);

    if (HalNetTransmit(Frame, sizeof(Frame)) != 0) {
        return -1;
    }

    Tries = (TimeoutMs > 0) ? (UINT32)(TimeoutMs / 2) : 1000u;
    for (Spin = 0; (UINT32)Spin < Tries; Spin++) {
        RxLen = HalNetReceive(Rx, sizeof(Rx));
        if (RxLen > 0) {
            int Dummy = 0;
            ProcessRx(Rx, RxLen, 0, 0, 0, &Dummy);
            if (TargetIp == IP_GW && gGwMacOk) {
                Copy(Mac, gGwMac, 6);
                return 0;
            }
            /* 任意 ARP reply：若 spa==Target 已在 LearnArp；再查一次 */
            if (RxLen >= (int)(ETH_HDR + ARP_PKT) &&
                Rx[12] == 0x08 && Rx[13] == 0x06) {
                UINT32 Spa =
                    ((UINT32)Rx[ETH_HDR + 14] << 24) |
                    ((UINT32)Rx[ETH_HDR + 15] << 16) |
                    ((UINT32)Rx[ETH_HDR + 16] << 8) | (UINT32)Rx[ETH_HDR + 17];
                if (Spa == TargetIp) {
                    Copy(Mac, Rx + ETH_HDR + 8, 6);
                    if (TargetIp == IP_GW) {
                        Copy(gGwMac, Mac, 6);
                        gGwMacOk = 1;
                    }
                    return 0;
                }
            }
        }
        __asm__ volatile("pause");
    }
    return -1;
}

int NetworkPing(const char *Host, int TimeoutMs) {
    UINT32 Target;
    UINT8 DstMac[6];
    UINT8 MyMac[6];
    UINT8 Frame[ETH_HDR + IP_HDR + ICMP_HDR + PING_PAY];
    UINT8 Rx[1518];
    UINT16 IpTotal;
    UINT16 Id;
    UINT16 Seq;
    UINTN i;
    int Spin;
    int RxLen;
    int Got = 0;
    UINT32 Tries;

    if (!HalNetReady()) {
        return -1;
    }
    if (Host == 0 || Host[0] == 0) {
        Target = IP_GW;
    } else if (NetworkParseIp(Host, &Target) != 0) {
        return -2;
    }
    if (ArpResolve(Target, DstMac, TimeoutMs > 0 ? TimeoutMs : 2000) != 0) {
        return -3;
    }

    HalNetGetMac(MyMac);
    Zero(Frame, sizeof(Frame));
    Copy(Frame, DstMac, 6);
    Copy(Frame + 6, MyMac, 6);
    Frame[12] = 0x08;
    Frame[13] = 0x00;

    Frame[ETH_HDR] = 0x45;
    Frame[ETH_HDR + 8] = 64; /* TTL */
    Frame[ETH_HDR + 9] = IP_ICMP;
    Frame[ETH_HDR + 12] = (UINT8)((IP_SELF >> 24) & 0xFFu);
    Frame[ETH_HDR + 13] = (UINT8)((IP_SELF >> 16) & 0xFFu);
    Frame[ETH_HDR + 14] = (UINT8)((IP_SELF >> 8) & 0xFFu);
    Frame[ETH_HDR + 15] = (UINT8)(IP_SELF & 0xFFu);
    Frame[ETH_HDR + 16] = (UINT8)((Target >> 24) & 0xFFu);
    Frame[ETH_HDR + 17] = (UINT8)((Target >> 16) & 0xFFu);
    Frame[ETH_HDR + 18] = (UINT8)((Target >> 8) & 0xFFu);
    Frame[ETH_HDR + 19] = (UINT8)(Target & 0xFFu);
    IpTotal = (UINT16)(IP_HDR + ICMP_HDR + PING_PAY);
    Frame[ETH_HDR + 2] = (UINT8)(IpTotal >> 8);
    Frame[ETH_HDR + 3] = (UINT8)(IpTotal & 0xFFu);
    Frame[ETH_HDR + 4] = 0x12;
    Frame[ETH_HDR + 5] = 0x34;
    {
        UINT16 Csum = Sum16(Frame + ETH_HDR, IP_HDR);
        Frame[ETH_HDR + 10] = (UINT8)(Csum >> 8);
        Frame[ETH_HDR + 11] = (UINT8)(Csum & 0xFFu);
    }

    gPingSeq++;
    Id = gPingId;
    Seq = gPingSeq;
    Frame[ETH_HDR + IP_HDR] = ICMP_ECHO_REQ;
    Frame[ETH_HDR + IP_HDR + 1] = 0;
    Frame[ETH_HDR + IP_HDR + 4] = (UINT8)(Id >> 8);
    Frame[ETH_HDR + IP_HDR + 5] = (UINT8)(Id & 0xFFu);
    Frame[ETH_HDR + IP_HDR + 6] = (UINT8)(Seq >> 8);
    Frame[ETH_HDR + IP_HDR + 7] = (UINT8)(Seq & 0xFFu);
    for (i = 0; i < PING_PAY; i++) {
        Frame[ETH_HDR + IP_HDR + ICMP_HDR + i] = (UINT8)i;
    }
    {
        UINT16 Csum = Sum16(Frame + ETH_HDR + IP_HDR, ICMP_HDR + PING_PAY);
        Frame[ETH_HDR + IP_HDR + 2] = (UINT8)(Csum >> 8);
        Frame[ETH_HDR + IP_HDR + 3] = (UINT8)(Csum & 0xFFu);
    }

    if (HalNetTransmit(Frame, ETH_HDR + IpTotal) != 0) {
        return -4;
    }

    Tries = (TimeoutMs > 0) ? (UINT32)(TimeoutMs / 2) : 1500u;
    for (Spin = 0; (UINT32)Spin < Tries; Spin++) {
        RxLen = HalNetReceive(Rx, sizeof(Rx));
        if (RxLen > 0) {
            ProcessRx(Rx, RxLen, Target, Id, Seq, &Got);
            if (Got) {
                return 0;
            }
        }
        __asm__ volatile("pause");
    }
    return -5;
}

#else

int NetworkParseIp(const char *S, UINT32 *Out) {
    (void)S;
    (void)Out;
    return -1;
}

int NetworkPing(const char *Host, int TimeoutMs) {
    (void)Host;
    (void)TimeoutMs;
    return -1;
}

#endif
