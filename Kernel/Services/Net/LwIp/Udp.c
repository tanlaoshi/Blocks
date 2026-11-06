/*
 * Udp.c — 极简 UDP
 */
#include "Compiler.h"
#include "Udp.h"
#include "HalDevices.h"

#define UDP_HDR_LEN 8
#define UDP_RX_QUEUE 8

typedef struct {
    UINT16 SrcPort;
    UINT16 DstPort;
    UINT16 Length;
    UINT16 Checksum;
} BLOCKS_PACKED UDP_HDR;

static UINT16 gBindPort;
static UDP_DATAGRAM gReceiveQueue[UDP_RX_QUEUE];
static int gReceiveHead;
static int gReceiveTail;
static int gReceiveCount;

void UdpInitialize(void) {
    gBindPort = 0;
    gReceiveHead = gReceiveTail = gReceiveCount = 0;
}

int UdpBind(UINT16 Port) {
    gBindPort = Port;
    gReceiveHead = gReceiveTail = gReceiveCount = 0;
    return 0;
}

UINT16 UdpBoundPort(void) {
    return gBindPort;
}

int UdpSend(UINT32 DstIp, UINT16 DstPort, const void *Data, UINTN Len) {
    UINT8 Buf[UDP_HDR_LEN + UDP_PAYLOAD_MAX];
    UDP_HDR *Hdr;
    UINT16 SrcPort;

    if (!HalNetReady() || Data == 0 || Len > UDP_PAYLOAD_MAX) {
        return -1;
    }
    SrcPort = gBindPort != 0 ? gBindPort : 40000;
    Hdr = (UDP_HDR *)Buf;
    Hdr->SrcPort = (UINT16)((SrcPort >> 8) | (SrcPort << 8));
    Hdr->DstPort = (UINT16)((DstPort >> 8) | (DstPort << 8));
    Hdr->Length = (UINT16)(((UDP_HDR_LEN + Len) >> 8) | ((UDP_HDR_LEN + Len) << 8));
    Hdr->Checksum = 0; /* IPv4 允许 UDP 校验和为 0 */
    {
        UINT8 *P = Buf + UDP_HDR_LEN;
        const UINT8 *S = (const UINT8 *)Data;
        UINTN i;
        for (i = 0; i < Len; i++) {
            P[i] = S[i];
        }
    }
    return HalNetSendIp(DstIp, HAL_IP_PROTO_UDP, Buf, UDP_HDR_LEN + Len);
}

int UdpRecv(UDP_DATAGRAM *Out) {
    if (!Out || gReceiveCount == 0) {
        return 0;
    }
    *Out = gReceiveQueue[gReceiveHead];
    gReceiveHead = (gReceiveHead + 1) % UDP_RX_QUEUE;
    gReceiveCount--;
    return 1;
}

void UdpInput(UINT32 SrcIp, UINT32 DstIp, const UINT8 *Payload, UINTN Len) {
    const UDP_HDR *Hdr;
    UINT16 DstPort;
    UINT16 SrcPort;
    UINT16 UdpLen;
    UINTN DataLen;

    (void)DstIp;
    if (Len < UDP_HDR_LEN) {
        return;
    }
    Hdr = (const UDP_HDR *)Payload;
    DstPort = (UINT16)((Hdr->DstPort >> 8) | (Hdr->DstPort << 8));
    SrcPort = (UINT16)((Hdr->SrcPort >> 8) | (Hdr->SrcPort << 8));
    UdpLen = (UINT16)((Hdr->Length >> 8) | (Hdr->Length << 8));
    if (gBindPort != 0 && DstPort != gBindPort) {
        return;
    }
    if (UdpLen < UDP_HDR_LEN || UdpLen > Len) {
        return;
    }
    DataLen = UdpLen - UDP_HDR_LEN;
    if (DataLen > UDP_PAYLOAD_MAX) {
        DataLen = UDP_PAYLOAD_MAX;
    }
    if (gReceiveCount >= UDP_RX_QUEUE) {
        gReceiveHead = (gReceiveHead + 1) % UDP_RX_QUEUE;
        gReceiveCount--;
    }
    gReceiveQueue[gReceiveTail].SrcIp = SrcIp;
    gReceiveQueue[gReceiveTail].SrcPort = SrcPort;
    gReceiveQueue[gReceiveTail].DstPort = DstPort;
    gReceiveQueue[gReceiveTail].Len = (UINT16)DataLen;
    {
        UINTN i;
        for (i = 0; i < DataLen; i++) {
            gReceiveQueue[gReceiveTail].Data[i] = Payload[UDP_HDR_LEN + i];
        }
    }
    gReceiveTail = (gReceiveTail + 1) % UDP_RX_QUEUE;
    gReceiveCount++;
}
