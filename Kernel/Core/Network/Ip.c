/*
 * Ip.c — K39：共用 ARP / 发 IPv4 载荷（ping/UDP 共用）
 *
 * 【初学者】
 * 本机固定 10.0.2.15（QEMU user）；出站先 ARP 再封以太网+IP。
 */
#include "Network.h"
#include "HalNet.h"

#if defined(__x86_64__) || defined(_M_X64)

#define ETH_HDR 14u
#define IP_HDR  20u
#define ARP_PKT 28u
#define ETH_ARP 0x0806u
#define ETH_IP  0x0800u
#define ARP_REQ 1u
#define ARP_REP 2u

#define ARP_CACHE 8u

typedef struct {
    UINT32 Ip;
    UINT8 Mac[6];
    int Valid;
} ARP_ENT;

static ARP_ENT gArp[ARP_CACHE];

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

UINT32 NetworkSelfIp(void) {
    return 0x0A00020Fu; /* 10.0.2.15 */
}

UINT32 NetworkGwIp(void) {
    return 0x0A000202u; /* 10.0.2.2 */
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

void NetworkArpLearn(UINT32 Ip, const UINT8 Mac[6]) {
    UINTN i;
    UINTN Slot = 0;
    if (Ip == 0 || Mac == 0) {
        return;
    }
    for (i = 0; i < ARP_CACHE; i++) {
        if (gArp[i].Valid && gArp[i].Ip == Ip) {
            Copy(gArp[i].Mac, Mac, 6);
            return;
        }
        if (!gArp[i].Valid) {
            Slot = i;
            break;
        }
        Slot = i;
    }
    gArp[Slot].Ip = Ip;
    Copy(gArp[Slot].Mac, Mac, 6);
    gArp[Slot].Valid = 1;
}

static int ArpLookup(UINT32 Ip, UINT8 Mac[6]) {
    UINTN i;
    for (i = 0; i < ARP_CACHE; i++) {
        if (gArp[i].Valid && gArp[i].Ip == Ip) {
            Copy(Mac, gArp[i].Mac, 6);
            return 0;
        }
    }
    return -1;
}

static void EatArpFrame(const UINT8 *Buf, int Len) {
    UINT16 Op;
    UINT32 Spa;
    if (Len < (int)(ETH_HDR + ARP_PKT)) {
        return;
    }
    if (Buf[12] != 0x08 || Buf[13] != 0x06) {
        return;
    }
    Op = ((UINT16)Buf[ETH_HDR + 6] << 8) | (UINT16)Buf[ETH_HDR + 7];
    Spa = ((UINT32)Buf[ETH_HDR + 14] << 24) | ((UINT32)Buf[ETH_HDR + 15] << 16) |
          ((UINT32)Buf[ETH_HDR + 16] << 8) | (UINT32)Buf[ETH_HDR + 17];
    if (Op == ARP_REP || Op == ARP_REQ) {
        NetworkArpLearn(Spa, Buf + ETH_HDR + 8);
    }
}

int NetworkArpResolve(UINT32 TargetIp, UINT8 Mac[6], int TimeoutMs) {
    UINT8 Frame[ETH_HDR + ARP_PKT];
    UINT8 MyMac[6];
    UINT8 Rx[1518];
    UINT32 Self = NetworkSelfIp();
    int Spin;
    int RxLen;
    UINT32 Tries;

    if (Mac == 0) {
        return -1;
    }
    if (ArpLookup(TargetIp, Mac) == 0) {
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
    Frame[21] = 0x01;
    Copy(Frame + 22, MyMac, 6);
    Frame[28] = (UINT8)((Self >> 24) & 0xFFu);
    Frame[29] = (UINT8)((Self >> 16) & 0xFFu);
    Frame[30] = (UINT8)((Self >> 8) & 0xFFu);
    Frame[31] = (UINT8)(Self & 0xFFu);
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
            EatArpFrame(Rx, RxLen);
            if (ArpLookup(TargetIp, Mac) == 0) {
                return 0;
            }
        }
        __asm__ volatile("pause");
    }
    return -1;
}

int NetworkSendIp(UINT32 DstIp, UINT8 Proto, const void *Payload, UINTN Len) {
    UINT8 Frame[ETH_HDR + IP_HDR + 1400];
    UINT8 DstMac[6];
    UINT8 MyMac[6];
    UINT32 Self = NetworkSelfIp();
    UINT16 IpTotal;
    UINTN i;
    const UINT8 *Src;

    if (!HalNetReady() || Payload == 0 || Len > 1400u) {
        return -1;
    }
    /* 同网段直送；否则走网关 MAC（课路径默认只打网关/本段） */
    if (NetworkArpResolve(DstIp, DstMac, 2000) != 0) {
        if (DstIp != NetworkGwIp() &&
            NetworkArpResolve(NetworkGwIp(), DstMac, 2000) != 0) {
            return -2;
        }
    }

    HalNetGetMac(MyMac);
    Zero(Frame, sizeof(Frame));
    Copy(Frame, DstMac, 6);
    Copy(Frame + 6, MyMac, 6);
    Frame[12] = 0x08;
    Frame[13] = 0x00;
    Frame[ETH_HDR] = 0x45;
    Frame[ETH_HDR + 8] = 64;
    Frame[ETH_HDR + 9] = Proto;
    Frame[ETH_HDR + 12] = (UINT8)((Self >> 24) & 0xFFu);
    Frame[ETH_HDR + 13] = (UINT8)((Self >> 16) & 0xFFu);
    Frame[ETH_HDR + 14] = (UINT8)((Self >> 8) & 0xFFu);
    Frame[ETH_HDR + 15] = (UINT8)(Self & 0xFFu);
    Frame[ETH_HDR + 16] = (UINT8)((DstIp >> 24) & 0xFFu);
    Frame[ETH_HDR + 17] = (UINT8)((DstIp >> 16) & 0xFFu);
    Frame[ETH_HDR + 18] = (UINT8)((DstIp >> 8) & 0xFFu);
    Frame[ETH_HDR + 19] = (UINT8)(DstIp & 0xFFu);
    IpTotal = (UINT16)(IP_HDR + Len);
    Frame[ETH_HDR + 2] = (UINT8)(IpTotal >> 8);
    Frame[ETH_HDR + 3] = (UINT8)(IpTotal & 0xFFu);
    Frame[ETH_HDR + 4] = 0x4F;
    Frame[ETH_HDR + 5] = 0x53;
    {
        UINT16 Csum = Sum16(Frame + ETH_HDR, IP_HDR);
        Frame[ETH_HDR + 10] = (UINT8)(Csum >> 8);
        Frame[ETH_HDR + 11] = (UINT8)(Csum & 0xFFu);
    }
    Src = (const UINT8 *)Payload;
    for (i = 0; i < Len; i++) {
        Frame[ETH_HDR + IP_HDR + i] = Src[i];
    }
    if (HalNetTransmit(Frame, ETH_HDR + IpTotal) != 0) {
        return -3;
    }
    return 0;
}

void NetworkEatArpFromFrame(const UINT8 *Buf, int Len) {
    EatArpFrame(Buf, Len);
}

#else

UINT32 NetworkSelfIp(void) {
    return 0;
}
UINT32 NetworkGwIp(void) {
    return 0;
}
int NetworkParseIp(const char *S, UINT32 *Out) {
    (void)S;
    (void)Out;
    return -1;
}
void NetworkArpLearn(UINT32 Ip, const UINT8 Mac[6]) {
    (void)Ip;
    (void)Mac;
}
int NetworkArpResolve(UINT32 TargetIp, UINT8 Mac[6], int TimeoutMs) {
    (void)TargetIp;
    (void)Mac;
    (void)TimeoutMs;
    return -1;
}
int NetworkSendIp(UINT32 DstIp, UINT8 Proto, const void *Payload, UINTN Len) {
    (void)DstIp;
    (void)Proto;
    (void)Payload;
    (void)Len;
    return -1;
}
void NetworkEatArpFromFrame(const UINT8 *Buf, int Len) {
    (void)Buf;
    (void)Len;
}

#endif
