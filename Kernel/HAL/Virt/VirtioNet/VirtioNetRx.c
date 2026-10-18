/*
 * VirtioNetRx.c — 接收环与入站分发（PR-S-virtionet-virt-1）
 */
#include "VirtioNet.h"
#include "VirtioNetPrivate.h"
#include "VirtioMmio.h"
#include "PhysicalMemory.h"
#include "HalSerial.h"
#include "Hal.h"
#include "Network/IpInput.h"
#include "Driver.h"
#include "DriverNet.h"
#include "ToySerialLog.h"
#ifdef TOY_LWIP
#include "toy_netif.h"
#endif

void RxRefillOne(UINT16 DescId, UINT8 *Buf) {
    UINT16 A;

    gRx.Desc[DescId].Addr = (UINT64)(UINTN)Buf;
    gRx.Desc[DescId].Len = RX_BUF_SIZE;
    gRx.Desc[DescId].Flags = VRING_DESC_F_WRITE;
    gRx.Desc[DescId].Next = 0;
    A = gRx.NextAvail;
    gRx.AvailRing[A % gRx.QueueSize] = DescId;
    __sync_synchronize();
    *gRx.AvailIdx = (UINT16)(A + 1);
    gRx.NextAvail = (UINT16)(A + 1);
}

void RxRefillAll(void) {
    UINT16 i;
    UINT16 N = gRx.QueueSize;
    if (N > RX_BUF_COUNT) {
        N = RX_BUF_COUNT;
    }
    for (i = 0; i < N; i++) {
        RxRefillOne(i, gRxBuf[i]);
    }
    VirtioMmioNotifyQueue(&gRx, RX_QUEUE_ID);
}

void NetProcessRx(void) {
    UINT16 Used = *gRx.UsedIdx;

    while (gRx.LastUsed != Used) {
        UINT16 Id = (UINT16)gRx.UsedRing[gRx.LastUsed % gRx.QueueSize].Id;
        UINT32 Len = gRx.UsedRing[gRx.LastUsed % gRx.QueueSize].Len;
        UINT8 *Buf = (UINT8 *)(UINTN)gRx.Desc[Id].Addr;

        if (Buf && Len > gNetHdrLen + ETH_HDR_LEN) {
            const UINT8 *Pkt = Buf + gNetHdrLen;
            UINTN PktLen = Len - gNetHdrLen;
#ifdef TOY_LWIP
            if (gLwIpRx) {
                ToyNetifInput(Pkt, PktLen);
            } else
#endif
            {
                IpInput(Pkt, PktLen);
            }
        }
        if (Id < RX_BUF_COUNT && gRxBuf[Id]) {
            RxRefillOne(Id, gRxBuf[Id]);
        }
        gRx.LastUsed = (UINT16)(gRx.LastUsed + 1);
    }
    VirtioMmioNotifyQueue(&gRx, RX_QUEUE_ID);
}
