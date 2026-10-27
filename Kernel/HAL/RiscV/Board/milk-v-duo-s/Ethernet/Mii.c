#include "Ethernet/Private.h"

int DuoMiiWaitIdle(void) {
    UINT32 Index;
    for (Index = 0; Index < 100000u; Index++) {
        if ((*DuoRegister(DUO_ETHERNET_BASE + DUO_ETHERNET_MII_ADDRESS) & DUO_ETHERNET_MII_BUSY) ==
            0u) {
            return 0;
        }
        DuoSpin(50u);
    }
    return -1;
}

int DuoMiiRead(UINT32 PhyAddress, UINT32 RegisterIndex, UINT16 *OutValue) {
    UINT32 Command;

    if (DuoMiiWaitIdle() != 0) {
        return -1;
    }
    Command = DUO_ETHERNET_MII_BUSY |
              ((PhyAddress & 0x1Fu) << DUO_ETHERNET_MII_PHY_SHIFT) |
              ((RegisterIndex & 0x1Fu) << DUO_ETHERNET_MII_REGISTER_SHIFT) |
              (DUO_ETHERNET_MII_CLOCK_RANGE << DUO_ETHERNET_MII_CLOCK_RANGE_SHIFT);
    *DuoRegister(DUO_ETHERNET_BASE + DUO_ETHERNET_MII_ADDRESS) = Command;
    if (DuoMiiWaitIdle() != 0) {
        return -1;
    }
    *OutValue =
        (UINT16)(*DuoRegister(DUO_ETHERNET_BASE + DUO_ETHERNET_MII_DATA) & 0xFFFFu);
    return 0;
}

int DuoMiiWrite(UINT32 PhyAddress, UINT32 RegisterIndex, UINT16 Value) {
    UINT32 Command;

    if (DuoMiiWaitIdle() != 0) {
        return -1;
    }
    *DuoRegister(DUO_ETHERNET_BASE + DUO_ETHERNET_MII_DATA) = (UINT32)Value;
    Command = DUO_ETHERNET_MII_BUSY | DUO_ETHERNET_MII_WRITE |
              ((PhyAddress & 0x1Fu) << DUO_ETHERNET_MII_PHY_SHIFT) |
              ((RegisterIndex & 0x1Fu) << DUO_ETHERNET_MII_REGISTER_SHIFT) |
              (DUO_ETHERNET_MII_CLOCK_RANGE << DUO_ETHERNET_MII_CLOCK_RANGE_SHIFT);
    *DuoRegister(DUO_ETHERNET_BASE + DUO_ETHERNET_MII_ADDRESS) = Command;
    return DuoMiiWaitIdle();
}
