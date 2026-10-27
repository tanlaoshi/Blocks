/*
 * Register.c — SMSC vendor REG / MII / EEPROM（bd-pi-eth）
 */
#include "Ethernet/Private.h"

void SmscDelayMilliseconds(UINT32 Milliseconds) {
    UINT32 m;
    volatile UINT32 i;

    for (m = 0; m < Milliseconds; m++) {
        for (i = 0; i < 80000u; i++) {
            __asm__ volatile("nop");
        }
    }
}

void SmscSelectDevice(void) {
    BoardDwc2SelectDevice(gSmscHub, gSmscPort, 0);
}

int SmscWriteRegister(UINT32 Index, UINT32 Data) {
    UINT32 Le = Data;
    int Ret;

    SmscSelectDevice();
    Ret = BoardDwc2Control(gSmscAddress, 0x40u, USB_VENDOR_WRITE_REG, 0, (UINT16)Index,
                           &Le, 4);
    return (Ret == 4) ? 0 : -1;
}

int SmscReadRegister(UINT32 Index, UINT32 *OutValue) {
    UINT32 Le = 0;
    int Ret;

    if (!OutValue) {
        return -1;
    }
    SmscSelectDevice();
    Ret = BoardDwc2Control(gSmscAddress, 0xC0u, USB_VENDOR_READ_REG, 0, (UINT16)Index,
                           &Le, 4);
    if (Ret != 4) {
        return -1;
    }
    *OutValue = Le;
    return 0;
}

static int MiiWait(void) {
    UINT32 V;
    UINT32 T;

    for (T = 0; T < 1000u; T++) {
        if (SmscReadRegister(MII_ADDR, &V) != 0) {
            return -1;
        }
        if ((V & MII_BUSY) == 0) {
            return 0;
        }
        SmscDelayMilliseconds(1);
    }
    return -1;
}

int SmscMiiRead(int Index, UINT16 *OutValue) {
    UINT32 Addr;
    UINT32 Value;

    if (MiiWait() != 0) {
        return -1;
    }
    Addr = ((UINT32)SMSC_PHY_ID << 11) | ((UINT32)Index << 6) | MII_BUSY;
    if (SmscWriteRegister(MII_ADDR, Addr) != 0 || MiiWait() != 0) {
        return -1;
    }
    if (SmscReadRegister(MII_DATA, &Value) != 0) {
        return -1;
    }
    if (OutValue) {
        *OutValue = (UINT16)(Value & 0xFFFFu);
    }
    return 0;
}

int SmscMiiWrite(int Index, UINT16 Value) {
    UINT32 Addr;

    if (MiiWait() != 0) {
        return -1;
    }
    if (SmscWriteRegister(MII_DATA, Value) != 0) {
        return -1;
    }
    Addr = ((UINT32)SMSC_PHY_ID << 11) | ((UINT32)Index << 6) | MII_BUSY |
           MII_WRITE;
    if (SmscWriteRegister(MII_ADDR, Addr) != 0) {
        return -1;
    }
    return MiiWait();
}

int SmscEepromRead(UINT32 Offset, UINT8 *Destination, UINT32 Length) {
    UINT32 i;
    UINT32 Cmd;
    UINT32 Data;
    UINT32 T;

    for (i = 0; i < Length; i++) {
        for (T = 0; T < 100u; T++) {
            if (SmscReadRegister(E2P_CMD, &Cmd) != 0) {
                return -1;
            }
            if ((Cmd & E2P_CMD_BUSY) == 0) {
                break;
            }
            SmscDelayMilliseconds(1);
        }
        Cmd = E2P_CMD_BUSY | E2P_CMD_READ | ((Offset + i) & E2P_CMD_ADDR);
        if (SmscWriteRegister(E2P_CMD, Cmd) != 0) {
            return -1;
        }
        for (T = 0; T < 100u; T++) {
            if (SmscReadRegister(E2P_CMD, &Cmd) != 0) {
                return -1;
            }
            if ((Cmd & E2P_CMD_BUSY) == 0) {
                break;
            }
            SmscDelayMilliseconds(1);
        }
        if (SmscReadRegister(E2P_DATA, &Data) != 0) {
            return -1;
        }
        Destination[i] = (UINT8)(Data & 0xFFu);
    }
    return 0;
}
