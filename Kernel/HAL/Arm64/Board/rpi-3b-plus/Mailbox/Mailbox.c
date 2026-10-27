/*
 * Mailbox.c — 电源/时钟/DRAM 属性（bd-pi-sd）
 */
#include "Mailbox/Private.h"

int BoardSdhciPowerOn(void) {
    static UINT32 W[8] __attribute__((aligned(16)));

    W[2] = MBOX_TAG_SET_POWER;
    W[3] = 8;
    W[4] = 0;
    W[5] = BOARD_MBOX_POWER_SDHCI;
    W[6] = MBOX_POWER_ON_WAIT;
    return MailboxProperty(W, 8);
}

int BoardSdhciPowerOff(void) {
    static UINT32 W[8] __attribute__((aligned(16)));

    W[2] = MBOX_TAG_SET_POWER;
    W[3] = 8;
    W[4] = 0;
    W[5] = BOARD_MBOX_POWER_SDHCI;
    W[6] = 2u; /* WAIT only → off */
    return MailboxProperty(W, 8);
}

int BoardMboxGetClockHz(UINT32 ClockId, UINT32 *OutHz) {
    static UINT32 W[8] __attribute__((aligned(16)));

    if (OutHz == 0) {
        return -1;
    }
    W[2] = MBOX_TAG_GET_CLOCK;
    W[3] = 8;
    W[4] = 0;
    W[5] = ClockId;
    W[6] = 0;
    if (MailboxProperty(W, 8) != 0) {
        return -1;
    }
    if (W[6] == 0) {
        return -1;
    }
    *OutHz = W[6];
    return 0;
}

int BoardDramProbe(UINT64 *Base, UINT64 *Size) {
    static UINT32 W[8] __attribute__((aligned(16)));
    UINT32 MemBase;
    UINT32 MemSize;

    if (Base == 0 || Size == 0) {
        return -1;
    }

    W[2] = MBOX_TAG_GET_ARM_MEM;
    W[3] = 8;
    W[4] = 0;
    W[5] = 0;
    W[6] = 0;

    if (MailboxProperty(W, 8) != 0) {
        return -1;
    }
    MemBase = W[5];
    MemSize = W[6];
    if (MemSize == 0) {
        return -1;
    }
    *Base = (UINT64)MemBase;
    *Size = (UINT64)MemSize;
    return 0;
}

