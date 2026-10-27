#include "Ethernet/Private.h"

EthernetDmaDescriptor gDuoTransmitDescriptor[DUO_ETHERNET_DMA_RING_COUNT]
    __attribute__((aligned(64)));
EthernetDmaDescriptor gDuoReceiveDescriptor[DUO_ETHERNET_DMA_RING_COUNT]
    __attribute__((aligned(64)));
UINT8 gDuoTransmitBuffer[DUO_ETHERNET_DMA_RING_COUNT][DUO_ETHERNET_DMA_BUFFER_SIZE]
    __attribute__((aligned(64)));
UINT8 gDuoReceiveBuffer[DUO_ETHERNET_DMA_RING_COUNT][DUO_ETHERNET_DMA_BUFFER_SIZE]
    __attribute__((aligned(64)));

int gDuoMacOk;
int gDuoPhyOk;
int gDuoDmaOk;
int gDuoBound;
UINT32 gDuoPhyAddress;
UINT8 gDuoMac[6];
UINT32 gDuoIp = HAL_NET_IP_DEFAULT;
UINT32 gDuoTransmitIndex;
UINT32 gDuoReceiveIndex;
UINT32 gDuoTransmitDone;
UINT32 gDuoReceiveFrames;
int gDuoLwIpReceive;
EthernetArpEntry gDuoArpCache[ETHERNET_ARP_CACHE_SIZE];
int gDuoPingWait;
UINT32 gDuoPingTarget;
UINT16 gDuoPingId = 0x4F53u;
UINT16 gDuoPingSequence;

void BoardEthernetRegister(void) {
    (void)DriverRegister(&gEthernetDriver);
}

int BoardEthernetGetLink(int *Up, UINT32 *Megabits, int *FullDuplex) {
    UINT16 Partner;
    int Link;

    if (!gDuoPhyOk) {
        return 0;
    }
    Link = DuoPhyLinkUp();
    if (Up) {
        *Up = Link;
    }
    if (Megabits) {
        *Megabits = 0;
    }
    if (FullDuplex) {
        *FullDuplex = 0;
    }
    if (!Link) {
        return 1;
    }
    if (DuoMiiRead(gDuoPhyAddress, DUO_PHY_LINK_PARTNER_ABILITY, &Partner) != 0) {
        Partner = 0;
    }
    if ((Partner & DUO_PHY_ADVERTISE_100_FULL) != 0) {
        if (Megabits) {
            *Megabits = 100u;
        }
        if (FullDuplex) {
            *FullDuplex = 1;
        }
    } else if ((Partner & DUO_PHY_ADVERTISE_100_HALF) != 0) {
        if (Megabits) {
            *Megabits = 100u;
        }
    } else if ((Partner & DUO_PHY_ADVERTISE_10_FULL) != 0) {
        if (Megabits) {
            *Megabits = 10u;
        }
        if (FullDuplex) {
            *FullDuplex = 1;
        }
    } else if ((Partner & DUO_PHY_ADVERTISE_10_HALF) != 0) {
        if (Megabits) {
            *Megabits = 10u;
        }
    }
    return 1;
}

int BoardEthernetProbe(void) {
    UINT32 SoftReset;
    UINT32 ClockEnableBefore;
    UINT32 ClockEnableAfter;
    UINT32 Divider;
    UINT32 Bypass;
    UINT32 UsbPhy;
    UINT32 MacConfig;
    UINT32 Version20;
    UINT32 Version110;
    UINT32 Version;

    HalSerialBootMark("Boot: Duo ETH probe\n");

    UsbPhy = *DuoRegister(0x03000048ULL);
    SoftReset = *DuoRegister(DUO_ETHERNET_TOP_SOFT_RESET);
    ClockEnableBefore = *DuoRegister(DUO_CLKGEN_BASE + DUO_CLK_ENABLE_0);
    Divider = *DuoRegister(DUO_CLKGEN_BASE + DUO_CLK_DIVIDER_500M_ETH0);
    Bypass = *DuoRegister(DUO_CLKGEN_BASE + DUO_CLK_BYPASS_0);

    DuoMarkHex("Boot: Duo ETH topusb=", UsbPhy);
    DuoMarkHex(" softrst=", SoftReset);
    DuoMarkHex(" clken0=", ClockEnableBefore);
    DuoMarkHex(" div500=", Divider);
    DuoMarkHex(" byp0=", Bypass);
    HalSerialBootMark("\n");

    SoftReset |= DUO_ETHERNET_SOFT_RESET_BIT;
    *DuoRegister(DUO_ETHERNET_TOP_SOFT_RESET) = SoftReset;
    DuoSpin(8000u);

    ClockEnableAfter =
        ClockEnableBefore | DUO_CLK_ENABLE_500M_ETH0 | DUO_CLK_ENABLE_AXI4_ETH0;
    *DuoRegister(DUO_CLKGEN_BASE + DUO_CLK_ENABLE_0) = ClockEnableAfter;
    if ((Divider & 0x000F0000u) == 0u) {
        Divider = (3u << 16) | (1u << 8);
        *DuoRegister(DUO_CLKGEN_BASE + DUO_CLK_DIVIDER_500M_ETH0) = Divider;
    }
    Bypass &= ~(1u << 9);
    *DuoRegister(DUO_CLKGEN_BASE + DUO_CLK_BYPASS_0) = Bypass;
    DuoSpin(8000u);

    ClockEnableAfter = *DuoRegister(DUO_CLKGEN_BASE + DUO_CLK_ENABLE_0);
    MacConfig = *DuoRegister(DUO_ETHERNET_BASE + DUO_ETHERNET_MAC_CONFIG);
    Version20 = *DuoRegister(DUO_ETHERNET_BASE + DUO_ETHERNET_VERSION);
    Version110 = *DuoRegister(DUO_ETHERNET_BASE + DUO_ETHERNET_GMAC4_VERSION);

    DuoMarkHex("Boot: Duo ETH clken0'=", ClockEnableAfter);
    DuoMarkHex(" mac0=", MacConfig);
    DuoMarkHex(" ver20=", Version20);
    DuoMarkHex(" ver110=", Version110);
    HalSerialBootMark("\n");

    if (DuoVersionIsLive(Version20)) {
        Version = Version20;
        HalSerialBootMark("Boot: Duo ETH id=3.70a@20 ver=");
    } else if (DuoVersionIsLive(Version110)) {
        Version = Version110;
        HalSerialBootMark("Boot: Duo ETH id=EQOS@110 ver=");
    } else {
        HalSerialBootMark("Boot: Duo ETH VERSION dead\n");
        return -1;
    }
    {
        char Buffer[9];
        HalSerialFormatHex(Buffer, Version, 8);
        HalSerialBootMark(Buffer);
    }
    HalSerialBootMark(" (MAC ok)\n");
    gDuoMacOk = 1;

    if (DuoPhyProbe() != 0) {
        return -1;
    }
    if (DuoDmaProbe() != 0) {
        return -1;
    }
    return 0;
}
