/*
 * Receive.c — 接收环与入站分发（PR-S-virtionet-virt-1）
 */
#include "Net.h"
#include "Private.h"
#include "Virtio/Virtio.h"
#include "PhysicalMemory.h"
#include "HalSerial.h"
#include "Hal.h"
#include "Network/IpInput.h"
#include "Driver.h"
#include "DriverNet.h"
#include "SerialLog.h"
#include "Atomic.h"
#ifdef LWIP_ENABLE
#include "Netif.h"
#endif

void ReceiveRefillOne(UINT16 DescId, UINT8 *Buf) {
    UINT16 A;

    gReceive.Desc[DescId].Addr = (UINT64)(UINTN)Buf;
    gReceive.Desc[DescId].Len = RX_BUF_SIZE;
    gReceive.Desc[DescId].Flags = VRING_DESC_F_WRITE;
    gReceive.Desc[DescId].Next = 0;
    A = gReceive.NextAvail;
    gReceive.AvailRing[A % gReceive.QueueSize] = DescId;
    BLOCKS_ATOMIC_FENCE();
    *gReceive.AvailIdx = (UINT16)(A + 1);
    gReceive.NextAvail = (UINT16)(A + 1);
}

void ReceiveRefillAll(void) {
    UINT16 i;
    UINT16 N = gReceive.QueueSize;
    if (N > RX_BUF_COUNT) {
        N = RX_BUF_COUNT;
    }
    for (i = 0; i < N; i++) {
        ReceiveRefillOne(i, gReceiveBuffer[i]);
    }
    VirtioMmioNotifyQueue(&gReceive, RX_QUEUE_ID);
}

void NetProcessReceive(void) {
    UINT16 Used = *gReceive.UsedIdx;

    while (gReceive.LastUsed != Used) {
        UINT16 Id = (UINT16)gReceive.UsedRing[gReceive.LastUsed % gReceive.QueueSize].Id;
        UINT32 Len = gReceive.UsedRing[gReceive.LastUsed % gReceive.QueueSize].Len;
        UINT8 *Buf = (UINT8 *)(UINTN)gReceive.Desc[Id].Addr;

        if (Buf && Len > gNetHdrLen + ETH_HDR_LEN) {
            const UINT8 *Pkt = Buf + gNetHdrLen;
            UINTN PktLen = Len - gNetHdrLen;
#ifdef LWIP_ENABLE
            if (gLwIpReceive) {
                LwIpNetifInput(Pkt, PktLen);
            } else
#endif
            {
                IpInput(Pkt, PktLen);
            }
        }
        if (Id < RX_BUF_COUNT && gReceiveBuffer[Id]) {
            ReceiveRefillOne(Id, gReceiveBuffer[Id]);
        }
        gReceive.LastUsed = (UINT16)(gReceive.LastUsed + 1);
    }
    VirtioMmioNotifyQueue(&gReceive, RX_QUEUE_ID);
}
