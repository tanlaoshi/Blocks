/*
 * Receive.c — 收包与轮询（PR-S-net-1）
 */
#include "Network/Private.h"
#include "Network/IpInput.h"
#ifdef LWIP_ENABLE
#include "Netif.h"
#endif
#include "PhysicalMemory.h"
#include "VirtualMemory.h"
#include "Serial/Serial.h"
#include "Debug.h"
#include "Hal.h"
#include "Driver.h"
#include "DriverNet.h"
#include "DriverNic.h"

static void NetProcessReceive(void) {
    UINT16 Head;
    UINT32 Len;
    UINT8 *Buf;
    UINT64 Phys;
    UINTN i;
    int Ok;

    while (VirtQueuePopUsed(&gReceiveQueue, &Head, &Len)) {
        Phys = gReceiveQueue.Desc[Head].Addr;
        Buf = (UINT8 *)(UINTN)Phys;
        /* Desc.Addr 是物理地址；恒等映射下可当指针，但必须落在 RX 池内 */
        Ok = 0;
        for (i = 0; i < RX_BUF_COUNT; i++) {
            if (Buf == gReceiveBufferData[i]) {
                Ok = 1;
                break;
            }
        }
        if (Ok && Len > VIRTIO_NET_HDR_LEN + ETH_HDR_LEN) {
            const UINT8 *Pkt = Buf + VIRTIO_NET_HDR_LEN;
            UINTN PktLen = Len - VIRTIO_NET_HDR_LEN;
#ifdef LWIP_ENABLE
            if (gLwIpReceive) {
                /* lwip on: all IP RX to lwIP; builtin IpInput idle */
                LwIpNetifInput(Pkt, PktLen);
            } else
#endif
            {
                IpInput(Pkt, PktLen);
            }
        }
        VirtQueueFreeDescriptor(&gReceiveQueue, Head);
        if (Ok) {
            ReceiveRefillOne(&gReceiveQueue, Buf);
        } else if (Head < RX_BUF_COUNT) {
            ReceiveRefillOne(&gReceiveQueue, gReceiveBufferData[Head]);
        }
    }
}

void NetInputFrame(const UINT8 *Pkt, UINTN Len) {
    if (!Pkt || Len < ETH_HDR_LEN) {
        return;
    }
#ifdef LWIP_ENABLE
    if (gLwIpReceive) {
        LwIpNetifInput(Pkt, Len);
        return;
    }
#endif
    IpInput(Pkt, Len);
}

void NetPoll(void) {
    UINT16 Head;
    UINT32 Len;
    UINT64 IrqFlags;

    if (!gNetOk) {
        return;
    }
    if (NetNicHasL2()) {
        NetNicPoll();
        return;
    }
    IrqFlags = HalIrqSave();
    if (gIsr) {
        *gIsr = 0;
    }
    while (VirtQueuePopUsed(&gTxQ, &Head, &Len)) {
        if (Head < gTxQ.Size) {
            VirtQueueFreeDescriptor(&gTxQ, Head);
            gTxDone++;
        }
        (void)Len;
    }
    NetProcessReceive();
    HalIrqRestore(IrqFlags);
}
