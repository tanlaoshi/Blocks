#include "Ethernet/Private.h"

int DuoPhyIdentityValid(UINT16 Identifier1, UINT16 Identifier2) {
    if (Identifier1 == 0u || Identifier1 == 0xFFFFu) {
        return 0;
    }
    if (Identifier2 == 0u || Identifier2 == 0xFFFFu) {
        return 0;
    }
    return 1;
}

int DuoPhyFind(UINT32 *OutAddress, UINT16 *OutIdentifier1,
                   UINT16 *OutIdentifier2) {
    static const UINT32 Prefer[] = { 1u, 0u };
    UINT32 Address;
    UINT32 Index;
    UINT16 Identifier1;
    UINT16 Identifier2;

    for (Index = 0; Index < sizeof(Prefer) / sizeof(Prefer[0]); Index++) {
        Address = Prefer[Index];
        if (DuoMiiRead(Address, DUO_PHY_IDENTIFIER_1, &Identifier1) != 0 ||
            DuoMiiRead(Address, DUO_PHY_IDENTIFIER_2, &Identifier2) != 0) {
            continue;
        }
        if (DuoPhyIdentityValid(Identifier1, Identifier2)) {
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
        if (DuoMiiRead(Address, DUO_PHY_IDENTIFIER_1, &Identifier1) != 0 ||
            DuoMiiRead(Address, DUO_PHY_IDENTIFIER_2, &Identifier2) != 0) {
            continue;
        }
        if (DuoPhyIdentityValid(Identifier1, Identifier2)) {
            *OutAddress = Address;
            *OutIdentifier1 = Identifier1;
            *OutIdentifier2 = Identifier2;
            return 0;
        }
    }
    return -1;
}

int DuoPhyReadBasicStatus(UINT32 Address, UINT16 *OutStatus) {
    UINT16 First;
    UINT16 Second;

    /* BMSR.Link 锁存：IEEE 要求读两次 */
    if (DuoMiiRead(Address, DUO_PHY_BASIC_STATUS, &First) != 0 ||
        DuoMiiRead(Address, DUO_PHY_BASIC_STATUS, &Second) != 0) {
        return -1;
    }
    *OutStatus = Second;
    return 0;
}

/*
 * IEEE clause 22 / Linux genphy_config_aneg：写 Advertise 再 ANENABLE|ANRESTART。
 * 不做 BMCR 软复位——会冲掉 cvitek config_init 的 ANA 页表。
 */
int DuoPhyRestartAutoneg(UINT32 Address) {
    UINT16 Control;

    if (DuoMiiWrite(Address, DUO_PHY_AUTONEG_ADVERTISE,
                 DUO_PHY_ADVERTISE_DEFAULT) != 0) {
        return -1;
    }
    if (DuoMiiRead(Address, DUO_PHY_BASIC_CONTROL, &Control) != 0) {
        return -1;
    }
    Control &= (UINT16)~DUO_PHY_BASIC_CONTROL_ISOLATE;
    Control |= (UINT16)(DUO_PHY_BASIC_CONTROL_AUTONEG_ENABLE |
                        DUO_PHY_BASIC_CONTROL_AUTONEG_RESTART |
                        DUO_PHY_BASIC_CONTROL_FULL_DUPLEX);
    return DuoMiiWrite(Address, DUO_PHY_BASIC_CONTROL, Control);
}

int DuoPhyProbe(void) {
    UINT32 Address;
    UINT16 Identifier1;
    UINT16 Identifier2;
    UINT16 BasicStatus;
    UINT16 Control;
    UINT32 Poll;
    int LinkUp;

    HalSerialBootMark("Boot: Duo ETH PHY\n");
    DuoPhyInitialize();
    DuoPhyConfigure();

    if (DuoPhyFind(&Address, &Identifier1, &Identifier2) != 0) {
        UINT16 Sample1;
        UINT16 Sample2;
        int ReadStatus;

        HalSerialBootMark("Boot: Duo ETH PHY none (MDIO)\n");
        ReadStatus = DuoMiiRead(1u, DUO_PHY_IDENTIFIER_1, &Sample1);
        if (ReadStatus != 0) {
            HalSerialBootMark("Boot: Duo ETH MDIO busy/fail @1\n");
        } else {
            (void)DuoMiiRead(1u, DUO_PHY_IDENTIFIER_2, &Sample2);
            HalSerialBootMark("Boot: Duo ETH MDIO@1 id=");
            DuoMarkHex16(Sample1);
            DuoMarkHex16(Sample2);
            HalSerialBootMark("\n");
        }
        return -1;
    }

    gDuoPhyAddress = Address;
    gDuoPhyOk = 1;
    DuoMarkHex("Boot: Duo ETH phy=", Address);
    HalSerialBootMark(" id=");
    DuoMarkHex16(Identifier1);
    DuoMarkHex16(Identifier2);
    HalSerialBootMark("\n");

    if (DuoPhyRestartAutoneg(Address) != 0) {
        HalSerialBootMark("Boot: Duo ETH AN restart fail\n");
        return -1;
    }
    if (DuoMiiRead(Address, DUO_PHY_BASIC_CONTROL, &Control) == 0) {
        DuoMarkHex("Boot: Duo ETH bmcr=", Control);
        HalSerialBootMark("\n");
    }

    LinkUp = 0;
    for (Poll = 0; Poll < 30u; Poll++) {
        DuoDelayMilliseconds(100u);
        if (DuoPhyReadBasicStatus(Address, &BasicStatus) != 0) {
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

        if (DuoMiiRead(Address, DUO_PHY_AUTONEG_ADVERTISE, &Advertise) != 0) {
            Advertise = 0;
        }
        if (DuoMiiRead(Address, DUO_PHY_LINK_PARTNER_ABILITY, &Partner) != 0) {
            Partner = 0;
        }
        DuoMarkHex("Boot: Duo ETH bmsr=", BasicStatus);
        DuoMarkHex(" adv=", Advertise);
        DuoMarkHex(" lpa=", Partner);
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
int DuoPhyLinkUp(void) {
    UINT16 BasicStatus;

    if (!gDuoPhyOk) {
        return 0;
    }
    if (DuoPhyReadBasicStatus(gDuoPhyAddress, &BasicStatus) != 0) {
        return 0;
    }
    return (BasicStatus & DUO_PHY_BASIC_STATUS_LINK) != 0;
}
