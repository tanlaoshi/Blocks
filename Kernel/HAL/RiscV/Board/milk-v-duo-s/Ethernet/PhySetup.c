#include "Ethernet/Private.h"

/*
 * U-Boot cv181x_ephy_id_init：放复位 / ANA 上电 / 写 PHY ID。
 * 末尾保持 APB，交给 Configure（Linux cvitek config_init）。
 */
void DuoPhyInitialize(void) {
    *DuoRegister(DUO_EPHY_APB_SELECT) = 1u;

    *DuoRegister(DUO_EPHY_CONTROL) = 0x0900u;
    *DuoRegister(DUO_EPHY_CONTROL) = 0x0904u;

    *DuoRegister(DUO_EPHY_PAGE_SELECT) = 0x0500u;
    *DuoRegister(DUO_EPHY_PAGE5_POWER) = 0x0c00u;
    *DuoRegister(DUO_EPHY_PAGE5_POWER) = 0x0c7eu;

    *DuoRegister(DUO_EPHY_CONTROL) = 0x0906u;

    *DuoRegister(DUO_EPHY_PAGE_SELECT) = 0x0500u;
    *DuoRegister(DUO_EPHY_APB_IDENTIFIER_1) = DUO_EPHY_IDENTIFIER_1_VALUE;
    *DuoRegister(DUO_EPHY_APB_IDENTIFIER_2) = DUO_EPHY_IDENTIFIER_2_VALUE;
}

/*
 * Linux drivers/net/phy/cvitek.c · cv182xa_phy_config_init（无 efuse 走 else 默认；
 * CONFIG_ARCH_CV181X LPF/HPF）。收尾 0x090e 启自协商 + BMCR 全双工，再交 MDIO。
 */
void DuoPhyConfigure(void) {
    *DuoRegister(DUO_EPHY_APB_SELECT) = 1u;
    *DuoRegister(DUO_EPHY_PAGE_SELECT) = 0x0000u;

    *DuoRegister(DUO_EPHY_APB(0x19u)) = 0x5a5au;
    *DuoRegister(DUO_EPHY_APB(0x15u)) = 0x0000u;
    *DuoRegister(DUO_EPHY_APB(0x16u)) = 0x0bb0u;

    *DuoRegister(DUO_EPHY_APB(0x17u)) = 0x0c10u;
    *DuoRegister(DUO_EPHY_APB(0x1Au)) = 0x0003u;
    *DuoRegister(DUO_EPHY_APB(0x15u)) = 0x0000u;

    *DuoRegister(DUO_EPHY_PAGE_SELECT) = 0x1000u;
    *DuoRegister(DUO_EPHY_APB(0x1Au)) = 0x1000u;
    *DuoRegister(DUO_EPHY_APB(0x1Bu)) = 0x3020u;
    *DuoRegister(DUO_EPHY_APB(0x1Cu)) = 0x5040u;
    *DuoRegister(DUO_EPHY_APB(0x1Du)) = 0x7060u;
    *DuoRegister(DUO_EPHY_APB(0x16u)) = 0x1708u;
    *DuoRegister(DUO_EPHY_APB(0x17u)) = 0x3827u;
    *DuoRegister(DUO_EPHY_APB(0x18u)) = 0x5748u;
    *DuoRegister(DUO_EPHY_APB(0x19u)) = 0x7867u;

    *DuoRegister(DUO_EPHY_PAGE_SELECT) = 0x1100u;
    *DuoRegister(DUO_EPHY_APB(0x10u)) = 0x9080u;
    *DuoRegister(DUO_EPHY_APB(0x11u)) = 0xb0a0u;
    *DuoRegister(DUO_EPHY_APB(0x12u)) = 0xd0c0u;
    *DuoRegister(DUO_EPHY_APB(0x13u)) = 0xf0e0u;
    *DuoRegister(DUO_EPHY_APB(0x14u)) = 0x9788u;
    *DuoRegister(DUO_EPHY_APB(0x15u)) = 0xb8a7u;
    *DuoRegister(DUO_EPHY_APB(0x16u)) = 0xd7c8u;
    *DuoRegister(DUO_EPHY_APB(0x17u)) = 0xf8e7u;

    *DuoRegister(DUO_EPHY_PAGE_SELECT) = 0x0500u;
    *DuoRegister(DUO_EPHY_PAGE5_POWER) =
        *DuoRegister(DUO_EPHY_PAGE5_POWER) | 0x0001u;
    *DuoRegister(DUO_EPHY_APB(0x13u)) =
        *DuoRegister(DUO_EPHY_APB(0x13u)) | 0x0820u;

    *DuoRegister(DUO_EPHY_PAGE_SELECT) = 0x0a00u;
    *DuoRegister(DUO_EPHY_APB(0x10u)) = 0x3e00u;
    *DuoRegister(DUO_EPHY_APB(0x11u)) = 0x7864u;
    *DuoRegister(DUO_EPHY_APB(0x12u)) = 0x6470u;
    *DuoRegister(DUO_EPHY_APB(0x13u)) = 0x5f62u;
    *DuoRegister(DUO_EPHY_APB(0x14u)) = 0x5a5au;
    *DuoRegister(DUO_EPHY_APB(0x15u)) = 0x5458u;
    *DuoRegister(DUO_EPHY_APB(0x16u)) = 0xb23au;
    *DuoRegister(DUO_EPHY_APB(0x17u)) = 0x94a0u;
    *DuoRegister(DUO_EPHY_APB(0x18u)) = 0x9092u;
    *DuoRegister(DUO_EPHY_APB(0x19u)) = 0x8a8eu;
    *DuoRegister(DUO_EPHY_APB(0x1Au)) = 0x8688u;
    *DuoRegister(DUO_EPHY_APB(0x1Bu)) = 0x8484u;
    *DuoRegister(DUO_EPHY_APB(0x1Cu)) = 0x0082u;

    *DuoRegister(DUO_EPHY_PAGE_SELECT) = 0x0b00u;
    *DuoRegister(DUO_EPHY_APB(0x10u)) = 0x5252u;
    *DuoRegister(DUO_EPHY_APB(0x11u)) = 0x5252u;
    *DuoRegister(DUO_EPHY_APB(0x12u)) = 0x4B52u;
    *DuoRegister(DUO_EPHY_APB(0x13u)) = 0x3D47u;
    *DuoRegister(DUO_EPHY_APB(0x14u)) = 0xAA99u;
    *DuoRegister(DUO_EPHY_APB(0x15u)) = 0x989Eu;
    *DuoRegister(DUO_EPHY_APB(0x16u)) = 0x9395u;
    *DuoRegister(DUO_EPHY_APB(0x17u)) = 0x9091u;
    *DuoRegister(DUO_EPHY_APB(0x18u)) = 0x8E8Fu;
    *DuoRegister(DUO_EPHY_APB(0x19u)) = 0x8D8Eu;
    *DuoRegister(DUO_EPHY_APB(0x1Au)) = 0x8C8Cu;
    *DuoRegister(DUO_EPHY_APB(0x1Bu)) = 0x8B8Bu;
    *DuoRegister(DUO_EPHY_APB(0x1Cu)) = 0x008Au;

    *DuoRegister(DUO_EPHY_PAGE_SELECT) = 0x0d00u;
    *DuoRegister(DUO_EPHY_APB(0x10u)) = 0x1E0Au;
    *DuoRegister(DUO_EPHY_APB(0x11u)) = 0x3862u;
    *DuoRegister(DUO_EPHY_APB(0x12u)) = 0x1E62u;
    *DuoRegister(DUO_EPHY_APB(0x13u)) = 0x2A08u;
    *DuoRegister(DUO_EPHY_APB(0x14u)) = 0x244Cu;
    *DuoRegister(DUO_EPHY_APB(0x15u)) = 0x1A44u;
    *DuoRegister(DUO_EPHY_APB(0x16u)) = 0x061Cu;

    *DuoRegister(DUO_EPHY_PAGE_SELECT) = 0x0e00u;
    *DuoRegister(DUO_EPHY_APB(0x10u)) = 0x2D30u;
    *DuoRegister(DUO_EPHY_APB(0x11u)) = 0x3470u;
    *DuoRegister(DUO_EPHY_APB(0x12u)) = 0x0648u;
    *DuoRegister(DUO_EPHY_APB(0x13u)) = 0x261Cu;
    *DuoRegister(DUO_EPHY_APB(0x14u)) = 0x3160u;
    *DuoRegister(DUO_EPHY_APB(0x15u)) = 0x2D5Eu;

    *DuoRegister(DUO_EPHY_PAGE_SELECT) = 0x0f00u;
    *DuoRegister(DUO_EPHY_APB(0x10u)) = 0x2922u;
    *DuoRegister(DUO_EPHY_APB(0x11u)) = 0x366Eu;
    *DuoRegister(DUO_EPHY_APB(0x12u)) = 0x0752u;
    *DuoRegister(DUO_EPHY_APB(0x13u)) = 0x2556u;
    *DuoRegister(DUO_EPHY_APB(0x14u)) = 0x2348u;
    *DuoRegister(DUO_EPHY_APB(0x15u)) = 0x0C30u;

    *DuoRegister(DUO_EPHY_PAGE_SELECT) = 0x1000u;
    *DuoRegister(DUO_EPHY_APB(0x10u)) = 0x1E08u;
    *DuoRegister(DUO_EPHY_APB(0x11u)) = 0x3868u;
    *DuoRegister(DUO_EPHY_APB(0x12u)) = 0x1462u;
    *DuoRegister(DUO_EPHY_APB(0x13u)) = 0x1A0Eu;
    *DuoRegister(DUO_EPHY_APB(0x14u)) = 0x305Eu;
    *DuoRegister(DUO_EPHY_APB(0x15u)) = 0x2F62u;

    *DuoRegister(DUO_EPHY_PAGE_SELECT) = 0x0100u;
    *DuoRegister(DUO_EPHY_APB(0x1Au)) =
        *DuoRegister(DUO_EPHY_APB(0x1Au)) & ~0x0f00u;

    *DuoRegister(DUO_EPHY_PAGE_SELECT) = 0x1300u;
    *DuoRegister(DUO_EPHY_APB(0x16u)) = 0x0012u;
    *DuoRegister(DUO_EPHY_APB(0x17u)) = 0x6848u;

    /* CV181x LPF/HPF（mars） */
    *DuoRegister(DUO_EPHY_PAGE_SELECT) = 0x1200u;
    *DuoRegister(DUO_EPHY_APB(0x12u)) = 0x0808u;
    *DuoRegister(DUO_EPHY_APB(0x13u)) = 0x0808u;
    *DuoRegister(DUO_EPHY_APB(0x14u)) = 0x32f8u;
    *DuoRegister(DUO_EPHY_APB(0x15u)) = 0xf8dcu;

    *DuoRegister(DUO_EPHY_PAGE_SELECT) = 0x0000u;
    *DuoRegister(DUO_EPHY_CONTROL) = DUO_EPHY_CONTROL_AUTONEG_START;
    *DuoRegister(DUO_EPHY_APB(DUO_PHY_BASIC_CONTROL)) =
        *DuoRegister(DUO_EPHY_APB(DUO_PHY_BASIC_CONTROL)) |
        DUO_PHY_BASIC_CONTROL_FULL_DUPLEX;

    *DuoRegister(DUO_EPHY_APB_SELECT) = 0u;
    DuoDelayMilliseconds(1u);
}
