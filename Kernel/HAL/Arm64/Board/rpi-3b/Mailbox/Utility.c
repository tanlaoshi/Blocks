/*
 * Utility.c — mailbox 寄存器与属性通道（bd-pi-sd）
 */
#include "Mailbox/Private.h"

volatile UINT32 *MailboxRegister(UINT32 Offset) {
    return (volatile UINT32 *)(UINTN)(MBOX_BASE + Offset);
}

void MailboxDataSynchronizationBarrier(void) {
    __asm__ volatile("dsb sy" ::: "memory");
}

void MailboxBootHexadecimal32(UINT32 Value) {
    char Buffer[9];
    HalSerialFormatHex(Buffer, Value, 8);
    HalSerialBootMark(Buffer);
}

/* 成功 0。Buffer 须 16 字节对齐；WordCount = 字数（含 hdr/end）。 */
int MailboxProperty(UINT32 *Buffer, UINT32 WordCount) {
    UINT32 Bus;
    UINT32 Spin;
    UINT32 Value;

    if (Buffer == 0 || WordCount < 3) {
        return -1;
    }

    Buffer[0] = WordCount * 4u;
    Buffer[1] = 0;
    Buffer[WordCount - 1] = 0;

    HalDcacheCleanInvalidateRange(Buffer, WordCount * 4u);
    Bus = PHYS_TO_BUS((UINTN)Buffer);

    for (Spin = 0; Spin < MBOX_SPIN_MAX; Spin++) {
        if ((*MailboxRegister(MBOX_STATUS0) & MBOX_EMPTY) != 0) {
            break;
        }
        (void)*MailboxRegister(MBOX_RD);
    }
    if (Spin >= MBOX_SPIN_MAX) {
        return -1;
    }

    for (Spin = 0; Spin < MBOX_SPIN_MAX; Spin++) {
        if ((*MailboxRegister(MBOX_STATUS1) & MBOX_FULL) == 0) {
            break;
        }
    }
    if (Spin >= MBOX_SPIN_MAX) {
        return -1;
    }

    *MailboxRegister(MBOX_WR) = (Bus & ~0xFu) | MBOX_CHAN_PROP;
    MailboxDataSynchronizationBarrier();

    for (Spin = 0; Spin < MBOX_SPIN_MAX; Spin++) {
        if ((*MailboxRegister(MBOX_STATUS0) & MBOX_EMPTY) == 0) {
            Value = *MailboxRegister(MBOX_RD);
            if ((Value & 0xFu) == MBOX_CHAN_PROP) {
                break;
            }
        }
    }
    if (Spin >= MBOX_SPIN_MAX) {
        return -1;
    }

    HalDcacheCleanInvalidateRange(Buffer, WordCount * 4u);
    if (Buffer[1] != MBOX_RESP_SUCCESS) {
        return -1;
    }
    return 0;
}

