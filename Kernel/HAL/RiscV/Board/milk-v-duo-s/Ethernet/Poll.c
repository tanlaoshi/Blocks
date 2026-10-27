#include "Ethernet/Private.h"

void DuoEthernetPoll(void) {
    UINT32 Index;
    UINT32 Guard;

    if (!gDuoDmaOk) {
        return;
    }
    for (Index = 0; Index < DUO_ETHERNET_DMA_RING_COUNT; Index++) {
        volatile EthernetDmaDescriptor *Descriptor = &gDuoTransmitDescriptor[Index];

        DuoDmaCacheSync((void *)Descriptor, sizeof(*Descriptor));
        if ((Descriptor->Descriptor0 & DUO_ETHERNET_DESCRIPTOR_OWN) != 0u) {
            continue;
        }
        if (Descriptor->Descriptor2 != 0u) {
            gDuoTransmitDone++;
            Descriptor->Descriptor2 = 0u;
            Descriptor->Descriptor1 =
                (Index + 1u == DUO_ETHERNET_DMA_RING_COUNT)
                    ? DUO_ETHERNET_TRANSMIT_END_RING
                    : 0u;
            DuoDmaCacheSync((void *)Descriptor, sizeof(*Descriptor));
        }
    }
    for (Guard = 0; Guard < DUO_ETHERNET_DMA_RING_COUNT; Guard++) {
        UINT32 ReceiveIndex = gDuoReceiveIndex;
        volatile EthernetDmaDescriptor *Descriptor =
            &gDuoReceiveDescriptor[ReceiveIndex];
        UINT32 Descriptor0;
        UINT32 FrameLength;

        DuoDmaCacheSync((void *)Descriptor, sizeof(*Descriptor));
        Descriptor0 = Descriptor->Descriptor0;
        if ((Descriptor0 & DUO_ETHERNET_DESCRIPTOR_OWN) != 0u) {
            break;
        }
        FrameLength = (Descriptor0 >>
                       DUO_ETHERNET_RECEIVE_DESCRIPTOR_FRAME_LENGTH_SHIFT) &
                      DUO_ETHERNET_RECEIVE_DESCRIPTOR_FRAME_LENGTH_MASK;
        if ((Descriptor0 & DUO_ETHERNET_RECEIVE_DESCRIPTOR_ERROR_SUMMARY) ==
                0u &&
            FrameLength >= ETHERNET_ETH_HEADER_LENGTH) {
            gDuoReceiveFrames++;
            DuoDmaCacheSync(gDuoReceiveBuffer[ReceiveIndex], FrameLength);
            DuoEthernetHandleFrame(gDuoReceiveBuffer[ReceiveIndex], FrameLength);
        }
        DuoDmaRearmReceive(ReceiveIndex);
        gDuoReceiveIndex = (ReceiveIndex + 1u) % DUO_ETHERNET_DMA_RING_COUNT;
        *DuoRegister(DUO_ETHERNET_BASE + DUO_ETHERNET_DMA_RECEIVE_POLL) = 1u;
    }
}

int DuoEthernetResolve(UINT32 TargetIp, UINT8 Mac[6], int TimeoutMilliseconds) {
    int Tries = TimeoutMilliseconds > 0 ? TimeoutMilliseconds / 10 : 100;

    if (DuoArpLookup(TargetIp, Mac)) {
        return 0;
    }
    DuoEthernetSendArpRequest(TargetIp);
    while (Tries-- > 0) {
        DuoEthernetPoll();
        if (DuoArpLookup(TargetIp, Mac)) {
            return 0;
        }
        HalCpuHalt();
    }
    return -1;
}
