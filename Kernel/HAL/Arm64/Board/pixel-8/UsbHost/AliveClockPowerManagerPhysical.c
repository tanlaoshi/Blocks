/*
 * UsbHost/AliveClockPowerManagerPhysical.c — zuma ACPM 开 eUSB LDO
 *
 * 对表：zuma.dtsi mailbox@15110000 + SRAM@15700000；s2mpg14 ch=2；
 * vdd075/12/33 = LDO8/9/19（L8M/L9M/L19M_EUSB）。
 */
#include "UsbHost/AliveClockPowerManagerPrivate.h"

#ifndef BOARD_PIXEL8_USB_HOST_BRINGUP
#define BOARD_PIXEL8_USB_HOST_BRINGUP 0
#endif

#if BOARD_PIXEL8_USB_HOST_BRINGUP

UINT8 UsbHostAliveClockPowerManagerLastResultCode(void) {
    return gAliveClockPowerLastResultCode;
}

UINT32 UsbHostAliveClockPowerManagerStatusBits(void) {
    return gAliveClockPowerStatusBits;
}

int UsbHostAliveClockPowerManagerEnableEmbeddedUsbRails(void) {
    UINT64 MailboxBase = ALIVE_CLOCK_POWER_MAILBOX_PHYSICAL_BASE;
    UINT64 SharedMemoryBase = ALIVE_CLOCK_POWER_SHARED_MEMORY_PHYSICAL_BASE;
    volatile AliveClockPowerFrameworkHeader *Framework;
    volatile AliveClockPowerChannelDescriptor *Channels;
    UINT32 ChannelMax;
    UINT32 ChannelIndex;
    UINT32 FoundIndex;
    UINT32 InterruptMask;
    AliveClockPowerRing Transmit;
    AliveClockPowerRing Receive;
    UINT8 SequenceNumber;
    int EnableResult;

    gAliveClockPowerLastResultCode = 0;
    gAliveClockPowerLastReturnCode = 0;
    gAliveClockPowerStatusBits = 0;

    HalSerialWriteShell("ac: ldo\r\n");
    Framework = (volatile AliveClockPowerFrameworkHeader *)(UINTN)(
        SharedMemoryBase + ALIVE_CLOCK_POWER_INITDATA_OFFSET);
    if (Framework->ApplicationProcessorChannelMax == 0u ||
        Framework->ApplicationProcessorChannelMax > 64u ||
        Framework->InterProcessChannelsOffset == 0u) {
        gAliveClockPowerLastResultCode = 0x10u;
        HalSerialWriteShell("ac: init\r\n");
        return -1;
    }
    gAliveClockPowerStatusBits |= (1u << 0);

    ChannelMax = Framework->ApplicationProcessorChannelMax;
    Channels = (volatile AliveClockPowerChannelDescriptor *)(UINTN)(
        SharedMemoryBase + Framework->InterProcessChannelsOffset);
    FoundIndex = 0xFFFFFFFFu;
    for (ChannelIndex = 0; ChannelIndex < ChannelMax; ChannelIndex++) {
        if (Channels[ChannelIndex].Identifier ==
            POWER_MANAGEMENT_INTEGRATED_CIRCUIT_CHANNEL) {
            FoundIndex = ChannelIndex;
            break;
        }
    }
    if (FoundIndex == 0xFFFFFFFFu) {
        gAliveClockPowerLastResultCode = 0x11u;
        HalSerialWriteShell("ac: ch\r\n");
        return -1;
    }
    gAliveClockPowerStatusBits |= (1u << 1);

    /* AP TX = APM RX（对表 channel_init 交换） */
    Transmit.Rear = (volatile UINT32 *)(UINTN)(
        SharedMemoryBase + Channels[FoundIndex].Channel.ReceiveRear);
    Transmit.Front = (volatile UINT32 *)(UINTN)(
        SharedMemoryBase + Channels[FoundIndex].Channel.ReceiveFront);
    Transmit.Base = (volatile UINT32 *)(UINTN)(
        SharedMemoryBase + Channels[FoundIndex].Channel.ReceiveBase);
    Transmit.ElementSizeBytes = Channels[FoundIndex].Channel.QueueElementSize;
    Transmit.QueueLength = Channels[FoundIndex].Channel.QueueLength;
    Transmit.HardwareIdentifier = Channels[FoundIndex].Identifier;

    Receive.Rear = (volatile UINT32 *)(UINTN)(
        SharedMemoryBase + Channels[FoundIndex].Channel.TransmitRear);
    Receive.Front = (volatile UINT32 *)(UINTN)(
        SharedMemoryBase + Channels[FoundIndex].Channel.TransmitFront);
    Receive.Base = (volatile UINT32 *)(UINTN)(
        SharedMemoryBase + Channels[FoundIndex].Channel.TransmitBase);
    Receive.ElementSizeBytes = Channels[FoundIndex].Channel.QueueElementSize;
    Receive.QueueLength = Channels[FoundIndex].Channel.QueueLength;
    Receive.HardwareIdentifier = Channels[FoundIndex].Identifier;

    InterruptMask = UsbHostReadMmio32(MailboxBase + MAILBOX_INTERRUPT_MASK0);
    InterruptMask |= (1u << Transmit.HardwareIdentifier);
    UsbHostWriteMmio32(MailboxBase + MAILBOX_INTERRUPT_MASK0, InterruptMask);
    gAliveClockPowerStatusBits |= (1u << 2);

    SequenceNumber = 0;
    EnableResult = AliveClockPowerEnableEmbeddedUsbRails(
        &Transmit, &Receive, MailboxBase, &SequenceNumber);
    if (EnableResult != 0) {
        gAliveClockPowerLastResultCode =
            (UINT8)(0x12u + (UINT8)(-EnableResult));
        HalSerialWriteShell("ac: wr\r\n");
        return -1;
    }

    gAliveClockPowerLastResultCode = 1u;
    HalSerialWriteShell("ac: ok\r\n");
    return 0;
}

#else

UINT8 UsbHostAliveClockPowerManagerLastResultCode(void) {
    return 0;
}

UINT32 UsbHostAliveClockPowerManagerStatusBits(void) {
    return 0;
}

int UsbHostAliveClockPowerManagerEnableEmbeddedUsbRails(void) {
    return -1;
}

#endif
