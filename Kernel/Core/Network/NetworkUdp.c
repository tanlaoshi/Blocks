/*
 * NetworkUdp.c — K39：极简 UDP（绑端口 / 发 / 收队列）
 *
 * 【初学者】
 * 对标现网 Udp.c：checksum 可填 0；收包进小队列。
 * 发往本机 IP 时本地回环入队，便于无外部发包时课验。
 */
#include "Network.h"
#include "HalNet.h"
#include "HalSerial.h"
#include "ToySerialConfig.h"

#define UDP_HDR_LEN 8u
#define UDP_RX_QUEUE 8u
#define IP_PROTO_UDP 17u
#define ETH_HDR 14u
#define IP_HDR 20u

static UINT16 gBindPort;
static NETWORK_UDP_DG gRxQ[UDP_RX_QUEUE];
static int gRxHead;
static int gRxTail;
static int gRxCount;

static void Copy(void *D, const void *S, UINTN N) {
    UINT8 *A = (UINT8 *)D;
    const UINT8 *B = (const UINT8 *)S;
    UINTN i;
    for (i = 0; i < N; i++) {
        A[i] = B[i];
    }
}

void NetworkUdpInitialize(void) {
    gBindPort = 0;
    gRxHead = gRxTail = gRxCount = 0;
}

int NetworkUdpBind(UINT16 Port) {
    gBindPort = Port;
    gRxHead = gRxTail = gRxCount = 0;
    return 0;
}

UINT16 NetworkUdpBoundPort(void) {
    return gBindPort;
}

static void Enqueue(UINT32 SrcIp, UINT16 SrcPort, UINT16 DstPort,
                    const UINT8 *Data, UINTN DataLen) {
    if (DataLen > NETWORK_UDP_PAYLOAD_MAX) {
        DataLen = NETWORK_UDP_PAYLOAD_MAX;
    }
    if (gRxCount >= (int)UDP_RX_QUEUE) {
        gRxHead = (gRxHead + 1) % (int)UDP_RX_QUEUE;
        gRxCount--;
    }
    gRxQ[gRxTail].SrcIp = SrcIp;
    gRxQ[gRxTail].SrcPort = SrcPort;
    gRxQ[gRxTail].DstPort = DstPort;
    gRxQ[gRxTail].Len = (UINT16)DataLen;
    Copy(gRxQ[gRxTail].Data, Data, DataLen);
    gRxTail = (gRxTail + 1) % (int)UDP_RX_QUEUE;
    gRxCount++;
}

void NetworkUdpInput(UINT32 SrcIp, UINT32 DstIp, const UINT8 *Payload, UINTN Len) {
    UINT16 DstPort;
    UINT16 SrcPort;
    UINT16 UdpLen;
    UINTN DataLen;

    (void)DstIp;
    if (Payload == 0 || Len < UDP_HDR_LEN) {
        return;
    }
    SrcPort = ((UINT16)Payload[0] << 8) | (UINT16)Payload[1];
    DstPort = ((UINT16)Payload[2] << 8) | (UINT16)Payload[3];
    UdpLen = ((UINT16)Payload[4] << 8) | (UINT16)Payload[5];
    if (gBindPort != 0 && DstPort != gBindPort) {
        return;
    }
    if (UdpLen < UDP_HDR_LEN || UdpLen > Len) {
        return;
    }
    DataLen = (UINTN)UdpLen - UDP_HDR_LEN;
    Enqueue(SrcIp, SrcPort, DstPort, Payload + UDP_HDR_LEN, DataLen);
}

void NetworkUdpInputFrame(const UINT8 *Frame, int Len) {
    UINT8 Ihl;
    UINT8 Proto;
    UINT32 SrcIp;
    UINT32 DstIp;
    UINT16 Tot;
    UINTN PayOff;
    UINTN PayLen;

    if (Frame == 0 || Len < (int)(ETH_HDR + IP_HDR + UDP_HDR_LEN)) {
        return;
    }
    if (Frame[12] != 0x08 || Frame[13] != 0x00) {
        return;
    }
    Ihl = (UINT8)((Frame[ETH_HDR] & 0x0Fu) * 4u);
    if (Ihl < IP_HDR) {
        return;
    }
    Proto = Frame[ETH_HDR + 9];
    if (Proto != IP_PROTO_UDP) {
        return;
    }
    Tot = ((UINT16)Frame[ETH_HDR + 2] << 8) | (UINT16)Frame[ETH_HDR + 3];
    SrcIp = ((UINT32)Frame[ETH_HDR + 12] << 24) | ((UINT32)Frame[ETH_HDR + 13] << 16) |
            ((UINT32)Frame[ETH_HDR + 14] << 8) | (UINT32)Frame[ETH_HDR + 15];
    DstIp = ((UINT32)Frame[ETH_HDR + 16] << 24) | ((UINT32)Frame[ETH_HDR + 17] << 16) |
            ((UINT32)Frame[ETH_HDR + 18] << 8) | (UINT32)Frame[ETH_HDR + 19];
    PayOff = ETH_HDR + Ihl;
    if ((int)PayOff + (int)UDP_HDR_LEN > Len) {
        return;
    }
    PayLen = (Tot > Ihl) ? (UINTN)(Tot - Ihl) : 0;
    if ((int)(PayOff + PayLen) > Len) {
        PayLen = (UINTN)Len - PayOff;
    }
    NetworkUdpInput(SrcIp, DstIp, Frame + PayOff, PayLen);
}

int NetworkUdpSend(UINT32 DstIp, UINT16 DstPort, const void *Data, UINTN Len) {
    UINT8 Buf[UDP_HDR_LEN + NETWORK_UDP_PAYLOAD_MAX];
    UINT16 SrcPort;
    UINTN i;
    const UINT8 *S;

    if (!HalNetReady() || Data == 0 || Len > NETWORK_UDP_PAYLOAD_MAX) {
        return -1;
    }
    SrcPort = (gBindPort != 0) ? gBindPort : 40000u;
    Buf[0] = (UINT8)(SrcPort >> 8);
    Buf[1] = (UINT8)(SrcPort & 0xFFu);
    Buf[2] = (UINT8)(DstPort >> 8);
    Buf[3] = (UINT8)(DstPort & 0xFFu);
    {
        UINT16 Ulen = (UINT16)(UDP_HDR_LEN + Len);
        Buf[4] = (UINT8)(Ulen >> 8);
        Buf[5] = (UINT8)(Ulen & 0xFFu);
    }
    Buf[6] = 0;
    Buf[7] = 0; /* checksum 0 合法 */
    S = (const UINT8 *)Data;
    for (i = 0; i < Len; i++) {
        Buf[UDP_HDR_LEN + i] = S[i];
    }

    /* 本机回环：不依赖 SLIRP 折返 */
    if (DstIp == NetworkSelfIp()) {
        NetworkUdpInput(NetworkSelfIp(), DstIp, Buf, UDP_HDR_LEN + Len);
        return 0;
    }

    if (NetworkSendIp(DstIp, IP_PROTO_UDP, Buf, UDP_HDR_LEN + Len) != 0) {
        return -2;
    }
    return 0;
}

int NetworkUdpRecv(NETWORK_UDP_DG *Out) {
    if (Out == 0 || gRxCount == 0) {
        return 0;
    }
    *Out = gRxQ[gRxHead];
    gRxHead = (gRxHead + 1) % (int)UDP_RX_QUEUE;
    gRxCount--;
    return 1;
}

int NetworkUdpPoll(int TimeoutMs) {
    UINT8 Rx[1518];
    int Spin;
    int RxLen;
    UINT32 Tries;
    int Got = 0;

    Tries = (TimeoutMs > 0) ? (UINT32)(TimeoutMs / 2) : 1u;
    for (Spin = 0; (UINT32)Spin < Tries; Spin++) {
        RxLen = HalNetReceive(Rx, sizeof(Rx));
        if (RxLen > 0) {
            NetworkEatArpFromFrame(Rx, RxLen);
            {
                int Before = gRxCount;
                NetworkUdpInputFrame(Rx, RxLen);
                if (gRxCount > Before) {
                    Got = 1;
                }
            }
        }
        __asm__ volatile("pause");
    }
    return Got;
}
