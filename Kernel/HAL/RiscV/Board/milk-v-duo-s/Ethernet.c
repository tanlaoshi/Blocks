/*
 * Ethernet.c — Duo S 以太网 MAC（net-1）+ PHY/link（net-2）
 *
 * MDIO：dwmac1000 MII Address/Data；phy<<11 reg<<6（dwmac1000_core.c）。
 * PHY：优先地址 1（mainline internal_ephy@1），再扫 0..31；Basic Status 读两次消锁存。
 */
#include "Ethernet.h"
#include "Board.h"
#include "Hal.h"
#include "SerialLog.h"

static volatile UINT32 *Register(UINT64 Address) {
    return (volatile UINT32 *)(UINTN)Address;
}

static void Spin(UINT32 Count) {
    volatile UINT32 Index;
    for (Index = 0; Index < Count; Index++) {
        __asm__ volatile("nop");
    }
}

static void DelayMilliseconds(UINT32 Milliseconds) {
    UINT32 Index;
    for (Index = 0; Index < Milliseconds; Index++) {
        Spin(80000u);
    }
}

static void MarkHex(const char *Tag, UINT32 Value) {
    char Buffer[9];
    HalSerialBootMark(Tag);
    HalSerialFormatHex(Buffer, Value, 8);
    HalSerialBootMark(Buffer);
}

static void MarkHex16(UINT32 Value) {
    char Buffer[5];
    HalSerialFormatHex(Buffer, Value & 0xFFFFu, 4);
    HalSerialBootMark(Buffer);
}

static int VersionIsLive(UINT32 Version) {
    return Version != 0u && Version != 0xFFFFFFFFu;
}

static int MiiWaitIdle(void) {
    UINT32 Index;
    for (Index = 0; Index < 100000u; Index++) {
        if ((*Register(DUO_ETHERNET_BASE + DUO_ETHERNET_MII_ADDRESS) & DUO_ETHERNET_MII_BUSY) ==
            0u) {
            return 0;
        }
        Spin(50u);
    }
    return -1;
}

static int MiiRead(UINT32 PhyAddress, UINT32 RegisterIndex, UINT16 *OutValue) {
    UINT32 Command;

    if (MiiWaitIdle() != 0) {
        return -1;
    }
    Command = DUO_ETHERNET_MII_BUSY |
              ((PhyAddress & 0x1Fu) << DUO_ETHERNET_MII_PHY_SHIFT) |
              ((RegisterIndex & 0x1Fu) << DUO_ETHERNET_MII_REGISTER_SHIFT) |
              (DUO_ETHERNET_MII_CLOCK_RANGE << DUO_ETHERNET_MII_CLOCK_RANGE_SHIFT);
    *Register(DUO_ETHERNET_BASE + DUO_ETHERNET_MII_ADDRESS) = Command;
    if (MiiWaitIdle() != 0) {
        return -1;
    }
    *OutValue =
        (UINT16)(*Register(DUO_ETHERNET_BASE + DUO_ETHERNET_MII_DATA) & 0xFFFFu);
    return 0;
}

static int MiiWrite(UINT32 PhyAddress, UINT32 RegisterIndex, UINT16 Value) {
    UINT32 Command;

    if (MiiWaitIdle() != 0) {
        return -1;
    }
    *Register(DUO_ETHERNET_BASE + DUO_ETHERNET_MII_DATA) = (UINT32)Value;
    Command = DUO_ETHERNET_MII_BUSY | DUO_ETHERNET_MII_WRITE |
              ((PhyAddress & 0x1Fu) << DUO_ETHERNET_MII_PHY_SHIFT) |
              ((RegisterIndex & 0x1Fu) << DUO_ETHERNET_MII_REGISTER_SHIFT) |
              (DUO_ETHERNET_MII_CLOCK_RANGE << DUO_ETHERNET_MII_CLOCK_RANGE_SHIFT);
    *Register(DUO_ETHERNET_BASE + DUO_ETHERNET_MII_ADDRESS) = Command;
    return MiiWaitIdle();
}

/*
 * U-Boot cv181x_ephy_id_init：放复位 / ANA 上电 / 写 PHY ID。
 * 末尾保持 APB，交给 Configure（Linux cvitek config_init）。
 */
static void PhyInitialize(void) {
    *Register(DUO_EPHY_APB_SELECT) = 1u;

    *Register(DUO_EPHY_CONTROL) = 0x0900u;
    *Register(DUO_EPHY_CONTROL) = 0x0904u;

    *Register(DUO_EPHY_PAGE_SELECT) = 0x0500u;
    *Register(DUO_EPHY_PAGE5_POWER) = 0x0c00u;
    *Register(DUO_EPHY_PAGE5_POWER) = 0x0c7eu;

    *Register(DUO_EPHY_CONTROL) = 0x0906u;

    *Register(DUO_EPHY_PAGE_SELECT) = 0x0500u;
    *Register(DUO_EPHY_APB_IDENTIFIER_1) = DUO_EPHY_IDENTIFIER_1_VALUE;
    *Register(DUO_EPHY_APB_IDENTIFIER_2) = DUO_EPHY_IDENTIFIER_2_VALUE;
}

/*
 * Linux drivers/net/phy/cvitek.c · cv182xa_phy_config_init（无 efuse 走 else 默认；
 * CONFIG_ARCH_CV181X LPF/HPF）。收尾 0x090e 启自协商 + BMCR 全双工，再交 MDIO。
 */
static void PhyConfigure(void) {
    *Register(DUO_EPHY_APB_SELECT) = 1u;
    *Register(DUO_EPHY_PAGE_SELECT) = 0x0000u;

    *Register(DUO_EPHY_APB(0x19u)) = 0x5a5au;
    *Register(DUO_EPHY_APB(0x15u)) = 0x0000u;
    *Register(DUO_EPHY_APB(0x16u)) = 0x0bb0u;

    *Register(DUO_EPHY_APB(0x17u)) = 0x0c10u;
    *Register(DUO_EPHY_APB(0x1Au)) = 0x0003u;
    *Register(DUO_EPHY_APB(0x15u)) = 0x0000u;

    *Register(DUO_EPHY_PAGE_SELECT) = 0x1000u;
    *Register(DUO_EPHY_APB(0x1Au)) = 0x1000u;
    *Register(DUO_EPHY_APB(0x1Bu)) = 0x3020u;
    *Register(DUO_EPHY_APB(0x1Cu)) = 0x5040u;
    *Register(DUO_EPHY_APB(0x1Du)) = 0x7060u;
    *Register(DUO_EPHY_APB(0x16u)) = 0x1708u;
    *Register(DUO_EPHY_APB(0x17u)) = 0x3827u;
    *Register(DUO_EPHY_APB(0x18u)) = 0x5748u;
    *Register(DUO_EPHY_APB(0x19u)) = 0x7867u;

    *Register(DUO_EPHY_PAGE_SELECT) = 0x1100u;
    *Register(DUO_EPHY_APB(0x10u)) = 0x9080u;
    *Register(DUO_EPHY_APB(0x11u)) = 0xb0a0u;
    *Register(DUO_EPHY_APB(0x12u)) = 0xd0c0u;
    *Register(DUO_EPHY_APB(0x13u)) = 0xf0e0u;
    *Register(DUO_EPHY_APB(0x14u)) = 0x9788u;
    *Register(DUO_EPHY_APB(0x15u)) = 0xb8a7u;
    *Register(DUO_EPHY_APB(0x16u)) = 0xd7c8u;
    *Register(DUO_EPHY_APB(0x17u)) = 0xf8e7u;

    *Register(DUO_EPHY_PAGE_SELECT) = 0x0500u;
    *Register(DUO_EPHY_PAGE5_POWER) =
        *Register(DUO_EPHY_PAGE5_POWER) | 0x0001u;
    *Register(DUO_EPHY_APB(0x13u)) =
        *Register(DUO_EPHY_APB(0x13u)) | 0x0820u;

    *Register(DUO_EPHY_PAGE_SELECT) = 0x0a00u;
    *Register(DUO_EPHY_APB(0x10u)) = 0x3e00u;
    *Register(DUO_EPHY_APB(0x11u)) = 0x7864u;
    *Register(DUO_EPHY_APB(0x12u)) = 0x6470u;
    *Register(DUO_EPHY_APB(0x13u)) = 0x5f62u;
    *Register(DUO_EPHY_APB(0x14u)) = 0x5a5au;
    *Register(DUO_EPHY_APB(0x15u)) = 0x5458u;
    *Register(DUO_EPHY_APB(0x16u)) = 0xb23au;
    *Register(DUO_EPHY_APB(0x17u)) = 0x94a0u;
    *Register(DUO_EPHY_APB(0x18u)) = 0x9092u;
    *Register(DUO_EPHY_APB(0x19u)) = 0x8a8eu;
    *Register(DUO_EPHY_APB(0x1Au)) = 0x8688u;
    *Register(DUO_EPHY_APB(0x1Bu)) = 0x8484u;
    *Register(DUO_EPHY_APB(0x1Cu)) = 0x0082u;

    *Register(DUO_EPHY_PAGE_SELECT) = 0x0b00u;
    *Register(DUO_EPHY_APB(0x10u)) = 0x5252u;
    *Register(DUO_EPHY_APB(0x11u)) = 0x5252u;
    *Register(DUO_EPHY_APB(0x12u)) = 0x4B52u;
    *Register(DUO_EPHY_APB(0x13u)) = 0x3D47u;
    *Register(DUO_EPHY_APB(0x14u)) = 0xAA99u;
    *Register(DUO_EPHY_APB(0x15u)) = 0x989Eu;
    *Register(DUO_EPHY_APB(0x16u)) = 0x9395u;
    *Register(DUO_EPHY_APB(0x17u)) = 0x9091u;
    *Register(DUO_EPHY_APB(0x18u)) = 0x8E8Fu;
    *Register(DUO_EPHY_APB(0x19u)) = 0x8D8Eu;
    *Register(DUO_EPHY_APB(0x1Au)) = 0x8C8Cu;
    *Register(DUO_EPHY_APB(0x1Bu)) = 0x8B8Bu;
    *Register(DUO_EPHY_APB(0x1Cu)) = 0x008Au;

    *Register(DUO_EPHY_PAGE_SELECT) = 0x0d00u;
    *Register(DUO_EPHY_APB(0x10u)) = 0x1E0Au;
    *Register(DUO_EPHY_APB(0x11u)) = 0x3862u;
    *Register(DUO_EPHY_APB(0x12u)) = 0x1E62u;
    *Register(DUO_EPHY_APB(0x13u)) = 0x2A08u;
    *Register(DUO_EPHY_APB(0x14u)) = 0x244Cu;
    *Register(DUO_EPHY_APB(0x15u)) = 0x1A44u;
    *Register(DUO_EPHY_APB(0x16u)) = 0x061Cu;

    *Register(DUO_EPHY_PAGE_SELECT) = 0x0e00u;
    *Register(DUO_EPHY_APB(0x10u)) = 0x2D30u;
    *Register(DUO_EPHY_APB(0x11u)) = 0x3470u;
    *Register(DUO_EPHY_APB(0x12u)) = 0x0648u;
    *Register(DUO_EPHY_APB(0x13u)) = 0x261Cu;
    *Register(DUO_EPHY_APB(0x14u)) = 0x3160u;
    *Register(DUO_EPHY_APB(0x15u)) = 0x2D5Eu;

    *Register(DUO_EPHY_PAGE_SELECT) = 0x0f00u;
    *Register(DUO_EPHY_APB(0x10u)) = 0x2922u;
    *Register(DUO_EPHY_APB(0x11u)) = 0x366Eu;
    *Register(DUO_EPHY_APB(0x12u)) = 0x0752u;
    *Register(DUO_EPHY_APB(0x13u)) = 0x2556u;
    *Register(DUO_EPHY_APB(0x14u)) = 0x2348u;
    *Register(DUO_EPHY_APB(0x15u)) = 0x0C30u;

    *Register(DUO_EPHY_PAGE_SELECT) = 0x1000u;
    *Register(DUO_EPHY_APB(0x10u)) = 0x1E08u;
    *Register(DUO_EPHY_APB(0x11u)) = 0x3868u;
    *Register(DUO_EPHY_APB(0x12u)) = 0x1462u;
    *Register(DUO_EPHY_APB(0x13u)) = 0x1A0Eu;
    *Register(DUO_EPHY_APB(0x14u)) = 0x305Eu;
    *Register(DUO_EPHY_APB(0x15u)) = 0x2F62u;

    *Register(DUO_EPHY_PAGE_SELECT) = 0x0100u;
    *Register(DUO_EPHY_APB(0x1Au)) =
        *Register(DUO_EPHY_APB(0x1Au)) & ~0x0f00u;

    *Register(DUO_EPHY_PAGE_SELECT) = 0x1300u;
    *Register(DUO_EPHY_APB(0x16u)) = 0x0012u;
    *Register(DUO_EPHY_APB(0x17u)) = 0x6848u;

    /* CV181x LPF/HPF（mars） */
    *Register(DUO_EPHY_PAGE_SELECT) = 0x1200u;
    *Register(DUO_EPHY_APB(0x12u)) = 0x0808u;
    *Register(DUO_EPHY_APB(0x13u)) = 0x0808u;
    *Register(DUO_EPHY_APB(0x14u)) = 0x32f8u;
    *Register(DUO_EPHY_APB(0x15u)) = 0xf8dcu;

    *Register(DUO_EPHY_PAGE_SELECT) = 0x0000u;
    *Register(DUO_EPHY_CONTROL) = DUO_EPHY_CONTROL_AUTONEG_START;
    *Register(DUO_EPHY_APB(DUO_PHY_BASIC_CONTROL)) =
        *Register(DUO_EPHY_APB(DUO_PHY_BASIC_CONTROL)) |
        DUO_PHY_BASIC_CONTROL_FULL_DUPLEX;

    *Register(DUO_EPHY_APB_SELECT) = 0u;
    DelayMilliseconds(1u);
}

static int PhyIdentityValid(UINT16 Identifier1, UINT16 Identifier2) {
    if (Identifier1 == 0u || Identifier1 == 0xFFFFu) {
        return 0;
    }
    if (Identifier2 == 0u || Identifier2 == 0xFFFFu) {
        return 0;
    }
    return 1;
}

static int PhyFind(UINT32 *OutAddress, UINT16 *OutIdentifier1,
                   UINT16 *OutIdentifier2) {
    static const UINT32 Prefer[] = { 1u, 0u };
    UINT32 Address;
    UINT32 Index;
    UINT16 Identifier1;
    UINT16 Identifier2;

    for (Index = 0; Index < sizeof(Prefer) / sizeof(Prefer[0]); Index++) {
        Address = Prefer[Index];
        if (MiiRead(Address, DUO_PHY_IDENTIFIER_1, &Identifier1) != 0 ||
            MiiRead(Address, DUO_PHY_IDENTIFIER_2, &Identifier2) != 0) {
            continue;
        }
        if (PhyIdentityValid(Identifier1, Identifier2)) {
            *OutAddress = Address;
            *OutIdentifier1 = Identifier1;
            *OutIdentifier2 = Identifier2;
            return 0;
        }
    }
    for (Address = 0; Address < 32u; Address++) {
        if (Address == 0u || Address == 1u) {
            continue;
        }
        if (MiiRead(Address, DUO_PHY_IDENTIFIER_1, &Identifier1) != 0 ||
            MiiRead(Address, DUO_PHY_IDENTIFIER_2, &Identifier2) != 0) {
            continue;
        }
        if (PhyIdentityValid(Identifier1, Identifier2)) {
            *OutAddress = Address;
            *OutIdentifier1 = Identifier1;
            *OutIdentifier2 = Identifier2;
            return 0;
        }
    }
    return -1;
}

static int PhyReadBasicStatus(UINT32 Address, UINT16 *OutStatus) {
    UINT16 First;
    UINT16 Second;

    /* BMSR.Link 锁存：IEEE 要求读两次 */
    if (MiiRead(Address, DUO_PHY_BASIC_STATUS, &First) != 0 ||
        MiiRead(Address, DUO_PHY_BASIC_STATUS, &Second) != 0) {
        return -1;
    }
    *OutStatus = Second;
    return 0;
}

/*
 * IEEE clause 22 / Linux genphy_config_aneg：写 Advertise 再 ANENABLE|ANRESTART。
 * 不做 BMCR 软复位——会冲掉 cvitek config_init 的 ANA 页表。
 */
static int PhyRestartAutoneg(UINT32 Address) {
    UINT16 Control;

    if (MiiWrite(Address, DUO_PHY_AUTONEG_ADVERTISE,
                 DUO_PHY_ADVERTISE_DEFAULT) != 0) {
        return -1;
    }
    if (MiiRead(Address, DUO_PHY_BASIC_CONTROL, &Control) != 0) {
        return -1;
    }
    Control &= (UINT16)~DUO_PHY_BASIC_CONTROL_ISOLATE;
    Control |= (UINT16)(DUO_PHY_BASIC_CONTROL_AUTONEG_ENABLE |
                        DUO_PHY_BASIC_CONTROL_AUTONEG_RESTART |
                        DUO_PHY_BASIC_CONTROL_FULL_DUPLEX);
    return MiiWrite(Address, DUO_PHY_BASIC_CONTROL, Control);
}

static int PhyProbe(void) {
    UINT32 Address;
    UINT16 Identifier1;
    UINT16 Identifier2;
    UINT16 BasicStatus;
    UINT16 Control;
    UINT32 Poll;
    int LinkUp;

    HalSerialBootMark("Boot: Duo ETH PHY\n");
    PhyInitialize();
    PhyConfigure();

    if (PhyFind(&Address, &Identifier1, &Identifier2) != 0) {
        UINT16 Sample1;
        UINT16 Sample2;
        int ReadStatus;

        HalSerialBootMark("Boot: Duo ETH PHY none (MDIO)\n");
        ReadStatus = MiiRead(1u, DUO_PHY_IDENTIFIER_1, &Sample1);
        if (ReadStatus != 0) {
            HalSerialBootMark("Boot: Duo ETH MDIO busy/fail @1\n");
        } else {
            (void)MiiRead(1u, DUO_PHY_IDENTIFIER_2, &Sample2);
            HalSerialBootMark("Boot: Duo ETH MDIO@1 id=");
            MarkHex16(Sample1);
            MarkHex16(Sample2);
            HalSerialBootMark("\n");
        }
        return -1;
    }

    MarkHex("Boot: Duo ETH phy=", Address);
    HalSerialBootMark(" id=");
    MarkHex16(Identifier1);
    MarkHex16(Identifier2);
    HalSerialBootMark("\n");

    if (PhyRestartAutoneg(Address) != 0) {
        HalSerialBootMark("Boot: Duo ETH AN restart fail\n");
        return -1;
    }
    if (MiiRead(Address, DUO_PHY_BASIC_CONTROL, &Control) == 0) {
        MarkHex("Boot: Duo ETH bmcr=", Control);
        HalSerialBootMark("\n");
    }

    LinkUp = 0;
    for (Poll = 0; Poll < 30u; Poll++) {
        DelayMilliseconds(100u);
        if (PhyReadBasicStatus(Address, &BasicStatus) != 0) {
            HalSerialBootMark("Boot: Duo ETH BasicStatus fail\n");
            return -1;
        }
        if ((BasicStatus & DUO_PHY_BASIC_STATUS_LINK) != 0) {
            LinkUp = 1;
            break;
        }
    }

    {
        UINT16 Advertise;
        UINT16 Partner;

        if (MiiRead(Address, DUO_PHY_AUTONEG_ADVERTISE, &Advertise) != 0) {
            Advertise = 0;
        }
        if (MiiRead(Address, DUO_PHY_LINK_PARTNER_ABILITY, &Partner) != 0) {
            Partner = 0;
        }
        MarkHex("Boot: Duo ETH bmsr=", BasicStatus);
        MarkHex(" adv=", Advertise);
        MarkHex(" lpa=", Partner);
        if ((BasicStatus & DUO_PHY_BASIC_STATUS_AUTONEG_COMPLETE) != 0) {
            HalSerialBootMark(" aneg=1");
        } else {
            HalSerialBootMark(" aneg=0");
        }
        if (LinkUp) {
            HalSerialBootMark(" link=up\n");
        } else {
            HalSerialBootMark(" link=down\n");
        }
    }
    return 0;
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

    UsbPhy = *Register(0x03000048ULL);
    SoftReset = *Register(DUO_ETHERNET_TOP_SOFT_RESET);
    ClockEnableBefore = *Register(DUO_CLKGEN_BASE + DUO_CLK_ENABLE_0);
    Divider = *Register(DUO_CLKGEN_BASE + DUO_CLK_DIVIDER_500M_ETH0);
    Bypass = *Register(DUO_CLKGEN_BASE + DUO_CLK_BYPASS_0);

    MarkHex("Boot: Duo ETH topusb=", UsbPhy);
    MarkHex(" softrst=", SoftReset);
    MarkHex(" clken0=", ClockEnableBefore);
    MarkHex(" div500=", Divider);
    MarkHex(" byp0=", Bypass);
    HalSerialBootMark("\n");

    SoftReset |= DUO_ETHERNET_SOFT_RESET_BIT;
    *Register(DUO_ETHERNET_TOP_SOFT_RESET) = SoftReset;
    Spin(8000u);

    ClockEnableAfter =
        ClockEnableBefore | DUO_CLK_ENABLE_500M_ETH0 | DUO_CLK_ENABLE_AXI4_ETH0;
    *Register(DUO_CLKGEN_BASE + DUO_CLK_ENABLE_0) = ClockEnableAfter;
    if ((Divider & 0x000F0000u) == 0u) {
        Divider = (3u << 16) | (1u << 8);
        *Register(DUO_CLKGEN_BASE + DUO_CLK_DIVIDER_500M_ETH0) = Divider;
    }
    Bypass &= ~(1u << 9);
    *Register(DUO_CLKGEN_BASE + DUO_CLK_BYPASS_0) = Bypass;
    Spin(8000u);

    ClockEnableAfter = *Register(DUO_CLKGEN_BASE + DUO_CLK_ENABLE_0);
    MacConfig = *Register(DUO_ETHERNET_BASE + DUO_ETHERNET_MAC_CONFIG);
    Version20 = *Register(DUO_ETHERNET_BASE + DUO_ETHERNET_VERSION);
    Version110 = *Register(DUO_ETHERNET_BASE + DUO_ETHERNET_GMAC4_VERSION);

    MarkHex("Boot: Duo ETH clken0'=", ClockEnableAfter);
    MarkHex(" mac0=", MacConfig);
    MarkHex(" ver20=", Version20);
    MarkHex(" ver110=", Version110);
    HalSerialBootMark("\n");

    if (VersionIsLive(Version20)) {
        Version = Version20;
        HalSerialBootMark("Boot: Duo ETH id=3.70a@20 ver=");
    } else if (VersionIsLive(Version110)) {
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

    (void)PhyProbe();
    return 0;
}
