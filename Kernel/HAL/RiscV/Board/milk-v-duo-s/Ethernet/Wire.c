#include "Ethernet/Private.h"

void DuoMacAdjustFromPhy(void) {
    UINT16 Partner = 0;
    UINT32 MacConfig;
    int FullDuplex = 1;
    int Speed100 = 1;

    if (gDuoPhyOk &&
        DuoMiiRead(gDuoPhyAddress, DUO_PHY_LINK_PARTNER_ABILITY, &Partner) == 0) {
        if ((Partner & DUO_PHY_ADVERTISE_100_FULL) != 0) {
            Speed100 = 1;
            FullDuplex = 1;
        } else if ((Partner & DUO_PHY_ADVERTISE_100_HALF) != 0) {
            Speed100 = 1;
            FullDuplex = 0;
        } else if ((Partner & DUO_PHY_ADVERTISE_10_FULL) != 0) {
            Speed100 = 0;
            FullDuplex = 1;
        } else if ((Partner & DUO_PHY_ADVERTISE_10_HALF) != 0) {
            Speed100 = 0;
            FullDuplex = 0;
        }
    }

    MacConfig = DUO_ETHERNET_MAC_PORT_SELECT_MII | DUO_ETHERNET_MAC_FRAME_BURST |
                DUO_ETHERNET_MAC_DISABLE_RECEIVE_OWN;
    if (Speed100) {
        MacConfig |= DUO_ETHERNET_MAC_SPEED_100;
    }
    if (FullDuplex) {
        MacConfig |= DUO_ETHERNET_MAC_DUPLEX;
    }
    MacConfig |= DUO_ETHERNET_MAC_TRANSMITTER_ENABLE |
                 DUO_ETHERNET_MAC_RECEIVER_ENABLE;
    *DuoRegister(DUO_ETHERNET_BASE + DUO_ETHERNET_MAC_CONFIG) = MacConfig;
    DuoMarkHex("Boot: Duo ETH mac=", MacConfig);
    HalSerialBootMark(Speed100 ? " 100" : " 10");
    HalSerialBootMark(FullDuplex ? "/FD\n" : "/HD\n");
}

int DuoDmaStartWire(void) {
    UINT32 Index;
    UINT32 Feature;
    UINT32 BusMode;
    UINT32 Operation;
    UINT32 Status;

    HalSerialBootMark("Boot: Duo ETH DMA wire\n");

    /* 停 MAC / DMA */
    *DuoRegister(DUO_ETHERNET_BASE + DUO_ETHERNET_MAC_CONFIG) = 0u;
    Operation = *DuoRegister(DUO_ETHERNET_BASE + DUO_ETHERNET_DMA_OPERATION_MODE);
    Operation &= ~(DUO_ETHERNET_DMA_START_TRANSMIT |
                   DUO_ETHERNET_DMA_START_RECEIVE);
    *DuoRegister(DUO_ETHERNET_BASE + DUO_ETHERNET_DMA_OPERATION_MODE) = Operation;

    Feature = *DuoRegister(DUO_ETHERNET_BASE + DUO_ETHERNET_DMA_HARDWARE_FEATURE);
    if ((Feature & DUO_ETHERNET_DMA_FEATURE_ENHANCED_DESCRIPTOR) != 0u) {
        return -1;
    }
    if (DuoDmaSoftwareReset() != 0) {
        HalSerialBootMark("Boot: Duo ETH DMA wire reset fail\n");
        return -1;
    }

    BusMode = (8u << DUO_ETHERNET_DMA_PBL_SHIFT);
    *DuoRegister(DUO_ETHERNET_BASE + DUO_ETHERNET_DMA_BUS_MODE) = BusMode;

    for (Index = 0; Index < DUO_ETHERNET_DMA_RING_COUNT; Index++) {
        DuoDmaRearmReceive(Index);
        gDuoTransmitDescriptor[Index].Descriptor0 = 0u;
        gDuoTransmitDescriptor[Index].Descriptor1 =
            (Index + 1u == DUO_ETHERNET_DMA_RING_COUNT)
                ? DUO_ETHERNET_TRANSMIT_END_RING
                : 0u;
        gDuoTransmitDescriptor[Index].Descriptor2 = 0u;
        gDuoTransmitDescriptor[Index].Descriptor3 = 0u;
    }
    DuoDmaCacheSync(gDuoTransmitDescriptor, sizeof(gDuoTransmitDescriptor));
    DuoDmaCacheSync(gDuoReceiveDescriptor, sizeof(gDuoReceiveDescriptor));

    *DuoRegister(DUO_ETHERNET_BASE + DUO_ETHERNET_DMA_RECEIVE_LIST_BASE) =
        (UINT32)(UINTN)&gDuoReceiveDescriptor[0];
    *DuoRegister(DUO_ETHERNET_BASE + DUO_ETHERNET_DMA_TRANSMIT_LIST_BASE) =
        (UINT32)(UINTN)&gDuoTransmitDescriptor[0];

    DuoDmaProgramMacAddress();
    *DuoRegister(DUO_ETHERNET_BASE + DUO_ETHERNET_FRAME_FILTER) =
        DUO_ETHERNET_FRAME_FILTER_PROMISCUOUS;

    Operation = DUO_ETHERNET_DMA_TRANSMIT_STORE_FORWARD |
                DUO_ETHERNET_DMA_RECEIVE_STORE_FORWARD |
                DUO_ETHERNET_DMA_OPERATE_SECOND_FRAME |
                DUO_ETHERNET_DMA_FLUSH_TRANSMIT_FIFO;
    *DuoRegister(DUO_ETHERNET_BASE + DUO_ETHERNET_DMA_OPERATION_MODE) = Operation;
    Operation |= DUO_ETHERNET_DMA_START_TRANSMIT |
                 DUO_ETHERNET_DMA_START_RECEIVE;
    *DuoRegister(DUO_ETHERNET_BASE + DUO_ETHERNET_DMA_OPERATION_MODE) = Operation;
    *DuoRegister(DUO_ETHERNET_BASE + DUO_ETHERNET_DMA_INTERRUPT_ENABLE) = 0u;

    DuoMacAdjustFromPhy();

    gDuoTransmitIndex = 0u;
    gDuoReceiveIndex = 0u;
    gDuoTransmitDone = 0u;
    gDuoReceiveFrames = 0u;
    Status = *DuoRegister(DUO_ETHERNET_BASE + DUO_ETHERNET_DMA_STATUS);
    *DuoRegister(DUO_ETHERNET_BASE + DUO_ETHERNET_DMA_STATUS) = Status;
    *DuoRegister(DUO_ETHERNET_BASE + DUO_ETHERNET_DMA_RECEIVE_POLL) = 1u;
    gDuoDmaOk = 1;
    HalSerialBootMark("Boot: Duo ETH DMA wire ready\n");
    return 0;
}

int BoardEthernetSendFrame(const UINT8 *Frame, UINTN FrameLength) {
    UINT32 Index;
    UINT32 Length;
    UINT32 TransmitControl;
    volatile EthernetDmaDescriptor *Descriptor;

    if (!gDuoDmaOk || Frame == 0 || FrameLength < 14u) {
        return -1;
    }
    Length = (UINT32)FrameLength;
    if (Length < 60u) {
        Length = 60u;
    }
    if (Length > DUO_ETHERNET_DMA_BUFFER_SIZE) {
        return -1;
    }
    Index = gDuoTransmitIndex;
    Descriptor = &gDuoTransmitDescriptor[Index];
    DuoDmaCacheSync((void *)Descriptor, sizeof(*Descriptor));
    if ((Descriptor->Descriptor0 & DUO_ETHERNET_DESCRIPTOR_OWN) != 0u) {
        return -1;
    }
    for (TransmitControl = 0; TransmitControl < Length; TransmitControl++) {
        gDuoTransmitBuffer[Index][TransmitControl] = 0u;
    }
    DuoMemoryCopy(gDuoTransmitBuffer[Index], Frame, FrameLength);
    DuoDmaCacheSync(gDuoTransmitBuffer[Index], Length);
    TransmitControl = (Length & DUO_ETHERNET_TRANSMIT_BUFFER1_SIZE_MASK) |
                      DUO_ETHERNET_TRANSMIT_FIRST_SEGMENT |
                      DUO_ETHERNET_TRANSMIT_LAST_SEGMENT |
                      DUO_ETHERNET_TRANSMIT_INTERRUPT;
    if (Index + 1u == DUO_ETHERNET_DMA_RING_COUNT) {
        TransmitControl |= DUO_ETHERNET_TRANSMIT_END_RING;
    }
    Descriptor->Descriptor2 = (UINT32)(UINTN)&gDuoTransmitBuffer[Index][0];
    Descriptor->Descriptor3 = 0u;
    Descriptor->Descriptor1 = TransmitControl;
    Descriptor->Descriptor0 = DUO_ETHERNET_DESCRIPTOR_OWN;
    DuoDmaCacheSync((void *)Descriptor, sizeof(*Descriptor));
    *DuoRegister(DUO_ETHERNET_BASE + DUO_ETHERNET_DMA_TRANSMIT_POLL) = 1u;
    gDuoTransmitIndex = (Index + 1u) % DUO_ETHERNET_DMA_RING_COUNT;
    return 0;
}
