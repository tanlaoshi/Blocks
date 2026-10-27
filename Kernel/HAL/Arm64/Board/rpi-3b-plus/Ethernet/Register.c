/*
 * Register.c — SMSC vendor REG / MII / EEPROM（bd-pi-eth）
 */
#include "Ethernet/Private.h"

void SmscDelayMs(UINT32 Ms) {
    UINT32 m;
    volatile UINT32 i;

    for (m = 0; m < Ms; m++) {
        for (i = 0; i < 80000u; i++) {
            __asm__ volatile("nop");
        }
    }
}

void SmscSelectDev(void) {
    BoardDwc2SelectDevice(gSmscHub, gSmscPort, 0);
}

int SmscWriteReg(UINT32 Index, UINT32 Data) {
    UINT32 Le = Data;
    int Ret;

    SmscSelectDev();
    Ret = BoardDwc2Control(gSmscAddr, 0x40u, USB_VENDOR_WRITE_REG, 0, (UINT16)Index,
                           &Le, 4);
    return (Ret == 4) ? 0 : -1;
}

int SmscReadReg(UINT32 Index, UINT32 *Out) {
    UINT32 Le = 0;
    int Ret;

    if (!Out) {
        return -1;
    }
    SmscSelectDev();
    Ret = BoardDwc2Control(gSmscAddr, 0xC0u, USB_VENDOR_READ_REG, 0, (UINT16)Index,
                           &Le, 4);
    if (Ret != 4) {
        return -1;
    }
    *Out = Le;
    return 0;
}

static int MiiWait(void) {
    UINT32 V;
    UINT32 T;

    for (T = 0; T < 1000u; T++) {
        if (SmscReadReg(MII_ADDR, &V) != 0) {
            return -1;
        }
        if ((V & MII_BUSY) == 0) {
            return 0;
        }
        SmscDelayMs(1);
    }
    return -1;
}

int SmscMiiRead(int Idx, UINT16 *Out) {
    UINT32 Addr;
    UINT32 Val;

    if (MiiWait() != 0) {
        return -1;
    }
    Addr = ((UINT32)SMSC_PHY_ID << 11) | ((UINT32)Idx << 6) | MII_BUSY;
    if (SmscWriteReg(MII_ADDR, Addr) != 0 || MiiWait() != 0) {
        return -1;
    }
    if (SmscReadReg(MII_DATA, &Val) != 0) {
        return -1;
    }
    if (Out) {
        *Out = (UINT16)(Val & 0xFFFFu);
    }
    return 0;
}

int SmscMiiWrite(int Idx, UINT16 Val) {
    UINT32 Addr;

    if (MiiWait() != 0) {
        return -1;
    }
    if (SmscWriteReg(MII_DATA, Val) != 0) {
        return -1;
    }
    Addr = ((UINT32)SMSC_PHY_ID << 11) | ((UINT32)Idx << 6) | MII_BUSY |
           MII_WRITE;
    if (SmscWriteReg(MII_ADDR, Addr) != 0) {
        return -1;
    }
    return MiiWait();
}

int SmscEepromRead(UINT32 Off, UINT8 *Dst, UINT32 Len) {
    UINT32 i;
    UINT32 Cmd;
    UINT32 Data;
    UINT32 T;

    for (i = 0; i < Len; i++) {
        for (T = 0; T < 100u; T++) {
            if (SmscReadReg(E2P_CMD, &Cmd) != 0) {
                return -1;
            }
            if ((Cmd & E2P_CMD_BUSY) == 0) {
                break;
            }
            SmscDelayMs(1);
        }
        Cmd = E2P_CMD_BUSY | E2P_CMD_READ | ((Off + i) & E2P_CMD_ADDR);
        if (SmscWriteReg(E2P_CMD, Cmd) != 0) {
            return -1;
        }
        for (T = 0; T < 100u; T++) {
            if (SmscReadReg(E2P_CMD, &Cmd) != 0) {
                return -1;
            }
            if ((Cmd & E2P_CMD_BUSY) == 0) {
                break;
            }
            SmscDelayMs(1);
        }
        if (SmscReadReg(E2P_DATA, &Data) != 0) {
            return -1;
        }
        Dst[i] = (UINT8)(Data & 0xFFu);
    }
    return 0;
}
