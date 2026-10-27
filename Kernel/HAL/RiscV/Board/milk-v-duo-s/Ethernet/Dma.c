#include "Ethernet/Private.h"

int DuoDmaSoftwareReset(void) {
    UINT32 Index;
    UINT32 BusMode;

    BusMode = *DuoRegister(DUO_ETHERNET_BASE + DUO_ETHERNET_DMA_BUS_MODE);
    *DuoRegister(DUO_ETHERNET_BASE + DUO_ETHERNET_DMA_BUS_MODE) =
        BusMode | DUO_ETHERNET_DMA_SOFTWARE_RESET;
    for (Index = 0; Index < 100000u; Index++) {
        if ((*DuoRegister(DUO_ETHERNET_BASE + DUO_ETHERNET_DMA_BUS_MODE) &
             DUO_ETHERNET_DMA_SOFTWARE_RESET) == 0u) {
            return 0;
        }
        DuoSpin(50u);
    }
    return -1;
}

void DuoDmaCacheSync(void *Address, UINTN Length) {
    HalDcacheCleanInvalidateRange(Address, Length);
}

int DuoDmaWaitOwnClear(volatile EthernetDmaDescriptor *Descriptor) {
    UINT32 Index;

    for (Index = 0; Index < 200000u; Index++) {
        DuoDmaCacheSync((void *)Descriptor, sizeof(*Descriptor));
        if ((Descriptor->Descriptor0 & DUO_ETHERNET_DESCRIPTOR_OWN) == 0u) {
            return 0;
        }
        DuoSpin(50u);
    }
    return -1;
}
void DuoDmaProgramMacAddress(void) {
    gDuoMac[0] = 0x02u;
    gDuoMac[1] = 0x00u;
    gDuoMac[2] = 0x00u;
    gDuoMac[3] = 0x00u;
    gDuoMac[4] = 0x00u;
    gDuoMac[5] = 0x01u;
    *DuoRegister(DUO_ETHERNET_BASE + DUO_ETHERNET_ADDRESS0_HIGH) =
        DUO_ETHERNET_ADDRESS_ENABLE | ((UINT32)gDuoMac[4] << 8) | gDuoMac[5];
    *DuoRegister(DUO_ETHERNET_BASE + DUO_ETHERNET_ADDRESS0_LOW) =
        ((UINT32)gDuoMac[0]) | ((UINT32)gDuoMac[1] << 8) | ((UINT32)gDuoMac[2] << 16) |
        ((UINT32)gDuoMac[3] << 24);
}

void DuoDmaRearmReceive(UINT32 Index) {
    gDuoReceiveDescriptor[Index].Descriptor1 =
        DUO_ETHERNET_DMA_BUFFER_SIZE & DUO_ETHERNET_RECEIVE_BUFFER1_SIZE_MASK;
    if (Index + 1u == DUO_ETHERNET_DMA_RING_COUNT) {
        gDuoReceiveDescriptor[Index].Descriptor1 |= DUO_ETHERNET_RECEIVE_END_RING;
    }
    gDuoReceiveDescriptor[Index].Descriptor2 =
        (UINT32)(UINTN)&gDuoReceiveBuffer[Index][0];
    gDuoReceiveDescriptor[Index].Descriptor3 = 0u;
    gDuoReceiveDescriptor[Index].Descriptor0 = DUO_ETHERNET_DESCRIPTOR_OWN;
    DuoDmaCacheSync(&gDuoReceiveDescriptor[Index], sizeof(gDuoReceiveDescriptor[Index]));
}

/*
 * net-3/4：normal 描述符环（本片 ENHDESSEL=0）。
 * 先 MAC LM 自检；通过后关 LM，环常驻（收发包槽）。
 */
int DuoDmaProbe(void) {
    UINT32 Index;
    UINT32 Feature;
    UINT32 BusMode;
    UINT32 Operation;
    UINT32 MacConfig;
    UINT32 Status;
    UINT32 FrameLength;
    UINT32 TransmitControl;

    HalSerialBootMark("Boot: Duo ETH DMA\n");

    Feature = *DuoRegister(DUO_ETHERNET_BASE + DUO_ETHERNET_DMA_HARDWARE_FEATURE);
    DuoMarkHex("Boot: Duo ETH dmafeat=", Feature);
    if ((Feature & DUO_ETHERNET_DMA_FEATURE_ENHANCED_DESCRIPTOR) != 0u) {
        HalSerialBootMark(" enh=1\n");
        HalSerialBootMark("Boot: Duo ETH DMA need normal (enh unsupported)\n");
        return -1;
    }
    HalSerialBootMark(" enh=0 normal\n");

    if (DuoDmaSoftwareReset() != 0) {
        HalSerialBootMark("Boot: Duo ETH DMA reset fail\n");
        return -1;
    }

    BusMode = (8u << DUO_ETHERNET_DMA_PBL_SHIFT);
    *DuoRegister(DUO_ETHERNET_BASE + DUO_ETHERNET_DMA_BUS_MODE) = BusMode;

    for (Index = 0; Index < DUO_ETHERNET_DMA_RING_COUNT; Index++) {
        gDuoReceiveDescriptor[Index].Descriptor1 =
            DUO_ETHERNET_DMA_BUFFER_SIZE & DUO_ETHERNET_RECEIVE_BUFFER1_SIZE_MASK;
        if (Index + 1u == DUO_ETHERNET_DMA_RING_COUNT) {
            gDuoReceiveDescriptor[Index].Descriptor1 |=
                DUO_ETHERNET_RECEIVE_END_RING;
        }
        gDuoReceiveDescriptor[Index].Descriptor2 =
            (UINT32)(UINTN)&gDuoReceiveBuffer[Index][0];
        gDuoReceiveDescriptor[Index].Descriptor3 = 0u;
        gDuoReceiveDescriptor[Index].Descriptor0 = DUO_ETHERNET_DESCRIPTOR_OWN;

        gDuoTransmitDescriptor[Index].Descriptor0 = 0u;
        gDuoTransmitDescriptor[Index].Descriptor1 = 0u;
        gDuoTransmitDescriptor[Index].Descriptor2 = 0u;
        gDuoTransmitDescriptor[Index].Descriptor3 = 0u;
    }
    DuoDmaCacheSync(gDuoReceiveDescriptor, sizeof(gDuoReceiveDescriptor));
    DuoDmaCacheSync(gDuoTransmitDescriptor, sizeof(gDuoTransmitDescriptor));
    DuoDmaCacheSync(gDuoReceiveBuffer, sizeof(gDuoReceiveBuffer));

    *DuoRegister(DUO_ETHERNET_BASE + DUO_ETHERNET_DMA_RECEIVE_LIST_BASE) =
        (UINT32)(UINTN)&gDuoReceiveDescriptor[0];
    *DuoRegister(DUO_ETHERNET_BASE + DUO_ETHERNET_DMA_TRANSMIT_LIST_BASE) =
        (UINT32)(UINTN)&gDuoTransmitDescriptor[0];

    DuoDmaProgramMacAddress();
    *DuoRegister(DUO_ETHERNET_BASE + DUO_ETHERNET_FRAME_FILTER) =
        DUO_ETHERNET_FRAME_FILTER_PROMISCUOUS;

    MacConfig = DUO_ETHERNET_MAC_PORT_SELECT_MII | DUO_ETHERNET_MAC_SPEED_100 |
                DUO_ETHERNET_MAC_DUPLEX | DUO_ETHERNET_MAC_LOOPBACK |
                DUO_ETHERNET_MAC_TRANSMITTER_ENABLE |
                DUO_ETHERNET_MAC_RECEIVER_ENABLE;
    *DuoRegister(DUO_ETHERNET_BASE + DUO_ETHERNET_MAC_CONFIG) = MacConfig;

    Operation = DUO_ETHERNET_DMA_TRANSMIT_STORE_FORWARD |
                DUO_ETHERNET_DMA_RECEIVE_STORE_FORWARD |
                DUO_ETHERNET_DMA_OPERATE_SECOND_FRAME |
                DUO_ETHERNET_DMA_START_TRANSMIT |
                DUO_ETHERNET_DMA_START_RECEIVE;
    *DuoRegister(DUO_ETHERNET_BASE + DUO_ETHERNET_DMA_OPERATION_MODE) = Operation;
    *DuoRegister(DUO_ETHERNET_BASE + DUO_ETHERNET_DMA_INTERRUPT_ENABLE) = 0u;

    /* 最小以太网帧：广播 + 本机 SA + ethertype 0x88B5 */
    for (Index = 0; Index < 64u; Index++) {
        gDuoTransmitBuffer[0][Index] = 0u;
    }
    for (Index = 0; Index < 6u; Index++) {
        gDuoTransmitBuffer[0][Index] = 0xFFu;
    }
    gDuoTransmitBuffer[0][6] = 0x02u;
    gDuoTransmitBuffer[0][12] = 0x88u;
    gDuoTransmitBuffer[0][13] = 0xB5u;
    gDuoTransmitBuffer[0][14] = 0x54u; /* 'T' */
    gDuoTransmitBuffer[0][15] = 0x4Fu; /* 'O' */
    gDuoTransmitBuffer[0][16] = 0x59u; /* 'Y' */
    DuoDmaCacheSync(gDuoTransmitBuffer[0], 64u);

    for (Index = 0; Index < DUO_ETHERNET_DMA_RING_COUNT; Index++) {
        gDuoTransmitDescriptor[Index].Descriptor0 = 0u;
        gDuoTransmitDescriptor[Index].Descriptor1 = 0u;
        gDuoTransmitDescriptor[Index].Descriptor2 = 0u;
        gDuoTransmitDescriptor[Index].Descriptor3 = 0u;
        if (Index + 1u == DUO_ETHERNET_DMA_RING_COUNT) {
            gDuoTransmitDescriptor[Index].Descriptor1 =
                DUO_ETHERNET_TRANSMIT_END_RING;
        }
    }
    /* normal：FS/LS/IC/size 在 TDES1；OWN 最后写 TDES0 */
    TransmitControl = (64u & DUO_ETHERNET_TRANSMIT_BUFFER1_SIZE_MASK) |
                      DUO_ETHERNET_TRANSMIT_FIRST_SEGMENT |
                      DUO_ETHERNET_TRANSMIT_LAST_SEGMENT |
                      DUO_ETHERNET_TRANSMIT_INTERRUPT;
    gDuoTransmitDescriptor[0].Descriptor2 =
        (UINT32)(UINTN)&gDuoTransmitBuffer[0][0];
    gDuoTransmitDescriptor[0].Descriptor3 = 0u;
    gDuoTransmitDescriptor[0].Descriptor1 = TransmitControl;
    gDuoTransmitDescriptor[0].Descriptor0 = DUO_ETHERNET_DESCRIPTOR_OWN;
    DuoDmaCacheSync(gDuoTransmitDescriptor, sizeof(gDuoTransmitDescriptor));

    *DuoRegister(DUO_ETHERNET_BASE + DUO_ETHERNET_DMA_TRANSMIT_POLL) = 1u;
    *DuoRegister(DUO_ETHERNET_BASE + DUO_ETHERNET_DMA_RECEIVE_POLL) = 1u;

    if (DuoDmaWaitOwnClear(&gDuoTransmitDescriptor[0]) != 0) {
        Status = *DuoRegister(DUO_ETHERNET_BASE + DUO_ETHERNET_DMA_STATUS);
        DuoMarkHex("Boot: Duo ETH DMA tx timeout st=", Status);
        HalSerialBootMark("\n");
        return -1;
    }
    HalSerialBootMark("Boot: Duo ETH DMA tx=ok\n");

    if (DuoDmaWaitOwnClear(&gDuoReceiveDescriptor[0]) != 0) {
        Status = *DuoRegister(DUO_ETHERNET_BASE + DUO_ETHERNET_DMA_STATUS);
        DuoMarkHex("Boot: Duo ETH DMA rx timeout st=", Status);
        HalSerialBootMark("\n");
        return -1;
    }

    DuoDmaCacheSync(&gDuoReceiveDescriptor[0], sizeof(gDuoReceiveDescriptor[0]));
    DuoDmaCacheSync(gDuoReceiveBuffer[0], DUO_ETHERNET_DMA_BUFFER_SIZE);
    if ((gDuoReceiveDescriptor[0].Descriptor0 &
         DUO_ETHERNET_RECEIVE_DESCRIPTOR_ERROR_SUMMARY) != 0u) {
        DuoMarkHex("Boot: Duo ETH DMA rx err des0=",
                gDuoReceiveDescriptor[0].Descriptor0);
        HalSerialBootMark("\n");
        return -1;
    }
    FrameLength =
        (gDuoReceiveDescriptor[0].Descriptor0 >>
         DUO_ETHERNET_RECEIVE_DESCRIPTOR_FRAME_LENGTH_SHIFT) &
        DUO_ETHERNET_RECEIVE_DESCRIPTOR_FRAME_LENGTH_MASK;
    DuoMarkHex("Boot: Duo ETH DMA rx=ok len=", FrameLength);
    HalSerialBootMark("\n");

    /*
     * LM 后整环重拉线缆模式（对表 U-Boot designware_eth_init）：
     * 停 TE/RE→DMA 复位→环→按 PHY LPA 写 MAC→开 SF/Flush→TE/RE。
     * 只清 LM 位不够：协商速度/双工与环回残留会让线缆收发静默失败。
     */
    return DuoDmaStartWire();
}
