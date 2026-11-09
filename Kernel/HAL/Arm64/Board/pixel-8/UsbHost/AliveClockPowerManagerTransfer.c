/*
 * UsbHost/AliveClockPowerManagerTransfer.c — ACPM MFD UPDATE + eUSB LDO8/9/19
 */
#include "UsbHost/AliveClockPowerManagerPrivate.h"

#ifndef BOARD_PIXEL8_USB_HOST_BRINGUP
#define BOARD_PIXEL8_USB_HOST_BRINGUP 0
#endif

#if BOARD_PIXEL8_USB_HOST_BRINGUP

UINT8 gAliveClockPowerLastResultCode;
UINT8 gAliveClockPowerLastReturnCode;
UINT32 gAliveClockPowerStatusBits;

void AliveClockPowerStallMicroseconds(UINT32 Microseconds) {
    UINT64 Frequency;
    UINT64 Start;
    UINT64 Delta;

    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(Frequency));
    if (Frequency == 0u) {
        UsbHostStallYield((UINTN)Microseconds * 100u);
        return;
    }
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(Start));
    Delta = (Frequency * (UINT64)Microseconds) / 1000000ull;
    for (;;) {
        UINT64 Now;
        __asm__ volatile("mrs %0, cntpct_el0" : "=r"(Now));
        if ((Now - Start) >= Delta) {
            break;
        }
    }
}

static void CopyWords(volatile UINT32 *Destination, const UINT32 *Source,
                      UINT32 WordCount) {
    UINT32 Index;
    for (Index = 0; Index < WordCount; Index++) {
        Destination[Index] = Source[Index];
    }
}

static int SendUpdate(AliveClockPowerRing *Transmit, AliveClockPowerRing *Receive,
                      UINT64 MailboxBase, UINT8 RegisterAddress, UINT8 Value,
                      UINT8 Mask, UINT8 *SequenceNumber) {
    UINT32 Command[4];
    UINT32 TransmitFront;
    UINT32 TransmitRear;
    UINT32 NextFront;
    UINT32 WordCount;
    UINT32 Guard;
    UINT8 Sequence;

    if (Transmit->QueueLength < 3u || Transmit->ElementSizeBytes < 16u) {
        return -1;
    }

    Sequence = (UINT8)(*SequenceNumber + 1u);
    if (Sequence == 0u || Sequence > SEQUENCE_NUMBER_MAX) {
        Sequence = 1u;
    }
    *SequenceNumber = Sequence;

    Command[0] =
        ((UINT32)POWER_MANAGEMENT_INTEGRATED_CIRCUIT_TYPE << 8) |
        ((UINT32)RegisterAddress) | ((UINT32)SPEEDY_CHANNEL << 12) |
        ((UINT32)Sequence << PROTOCOL_SEQUENCE_SHIFT);
    Command[1] = ((UINT32)FUNCTION_UPDATE) | ((UINT32)Value << 8) |
                 ((UINT32)Mask << 16);
    Command[2] = 0;
    Command[3] = 0;

    TransmitFront = *Transmit->Front;
    TransmitRear = *Transmit->Rear;
    NextFront = (TransmitFront + 1u) % Transmit->QueueLength;
    if (NextFront == TransmitRear) {
        return -2;
    }

    WordCount = Transmit->ElementSizeBytes / 4u;
    if (WordCount > 4u) {
        WordCount = 4u;
    }
    CopyWords(Transmit->Base + TransmitFront * (Transmit->ElementSizeBytes / 4u),
              Command, WordCount);
    __asm__ volatile("dsb sy" ::: "memory");
    *Transmit->Front = NextFront;
    __asm__ volatile("dsb sy" ::: "memory");
    UsbHostWriteMmio32(MailboxBase + MAILBOX_INTERRUPT_GROUP1,
                       1u << Transmit->HardwareIdentifier);

    for (Guard = 0; Guard < POLL_GUARD_MAX; Guard++) {
        UINT32 ReceiveFront = *Receive->Front;
        UINT32 ReceiveRear = *Receive->Rear;
        while (ReceiveRear != ReceiveFront) {
            volatile UINT32 *Slot =
                Receive->Base +
                ReceiveRear * (Receive->ElementSizeBytes / 4u);
            UINT32 Response0 = Slot[0];
            UINT32 Response1 = Slot[1];
            UINT8 ResponseSequence =
                (UINT8)((Response0 >> PROTOCOL_SEQUENCE_SHIFT) & 0x3Fu);
            ReceiveRear = (ReceiveRear + 1u) % Receive->QueueLength;
            *Receive->Rear = ReceiveRear;
            UsbHostWriteMmio32(MailboxBase + MAILBOX_INTERRUPT_CLEAR0,
                               1u << Receive->HardwareIdentifier);
            if (ResponseSequence == Sequence) {
                gAliveClockPowerLastReturnCode =
                    (UINT8)((Response1 >> 24) & 0xFFu);
                return (gAliveClockPowerLastReturnCode == 0u) ? 0 : -3;
            }
            ReceiveFront = *Receive->Front;
        }
        AliveClockPowerStallMicroseconds(1u);
    }
    return -4;
}

int AliveClockPowerEnableEmbeddedUsbRails(AliveClockPowerRing *Transmit,
                                          AliveClockPowerRing *Receive,
                                          UINT64 MailboxBase,
                                          UINT8 *SequenceNumber) {
    if (SendUpdate(Transmit, Receive, MailboxBase, LDO8_ENABLE_REGISTER,
                   LDO8_ENABLE_MASK, LDO8_ENABLE_MASK, SequenceNumber) != 0) {
        return -1;
    }
    gAliveClockPowerStatusBits |= (1u << 4);
    if (SendUpdate(Transmit, Receive, MailboxBase, LDO9_ENABLE_REGISTER,
                   LDO9_ENABLE_MASK, LDO9_ENABLE_MASK, SequenceNumber) != 0) {
        return -2;
    }
    gAliveClockPowerStatusBits |= (1u << 5);
    if (SendUpdate(Transmit, Receive, MailboxBase, LDO19_ENABLE_REGISTER,
                   LDO19_ENABLE_MASK, LDO19_ENABLE_MASK, SequenceNumber) !=
        0) {
        return -3;
    }
    gAliveClockPowerStatusBits |= (1u << 6);
    /* 对表 eusb_repeater_power_on：LDO 后 mdelay(3) */
    AliveClockPowerStallMicroseconds(3000u);
    return 0;
}

#else

UINT8 gAliveClockPowerLastResultCode;
UINT8 gAliveClockPowerLastReturnCode;
UINT32 gAliveClockPowerStatusBits;

void AliveClockPowerStallMicroseconds(UINT32 Microseconds) {
    (void)Microseconds;
}

int AliveClockPowerEnableEmbeddedUsbRails(AliveClockPowerRing *Transmit,
                                          AliveClockPowerRing *Receive,
                                          UINT64 MailboxBase,
                                          UINT8 *SequenceNumber) {
    (void)Transmit;
    (void)Receive;
    (void)MailboxBase;
    (void)SequenceNumber;
    return -1;
}

#endif
