/*
 * UsbHost/HubWatch.c — PR-MIX2-USB-2：根口上电/复位、编址 hub、热插循环
 *
 * 验收：§7.0 boot 后再插坞，splash 见 hub 0x1。禁止只扫一次。
 */
#include "UsbHost/Private.h"

#ifndef BOARD_MIX2_USB_HOST_BRINGUP
#define BOARD_MIX2_USB_HOST_BRINGUP 0
#endif

#if BOARD_MIX2_USB_HOST_BRINGUP

static UINT32 PortStatusNeutral(UINT32 State) {
    return (State & PORT_STATUS_READ_ONLY) | (State & PORT_STATUS_READ_WRITE_SET);
}

static void WriteLittleEndian32(UINT8 *Destination, UINT32 Value) {
    Destination[0] = (UINT8)(Value & 0xFFu);
    Destination[1] = (UINT8)((Value >> 8) & 0xFFu);
    Destination[2] = (UINT8)((Value >> 16) & 0xFFu);
    Destination[3] = (UINT8)((Value >> 24) & 0xFFu);
}

UINT32 UsbHostRootPortStatus(UINT32 PortIndexOneBased) {
    return UsbHostReadMmio32(UsbHostXhciPortStatusAddress(PortIndexOneBased));
}

int UsbHostRootPortPower(UINT32 PortIndexOneBased) {
    UINT64 Address = UsbHostXhciPortStatusAddress(PortIndexOneBased);
    UINT32 State = UsbHostReadMmio32(Address);

    if ((State & PORT_STATUS_POWER) == 0) {
        UsbHostWriteMmio32(Address, PortStatusNeutral(State) | PORT_STATUS_POWER);
        UsbHostStallYield(200000u);
        State = UsbHostReadMmio32(Address);
    }
    return (State & PORT_STATUS_POWER) ? 0 : -1;
}

int UsbHostRootPortReset(UINT32 PortIndexOneBased) {
    UINT64 Address = UsbHostXhciPortStatusAddress(PortIndexOneBased);
    UINT32 State;
    UINTN Guard;

    State = UsbHostReadMmio32(Address);
    UsbHostWriteMmio32(Address, PortStatusNeutral(State) | PORT_STATUS_RESET);
    for (Guard = 0; Guard < 2000000u; Guard++) {
        State = UsbHostReadMmio32(Address);
        if ((State & PORT_STATUS_RESET) == 0) {
            break;
        }
        UsbHostStallYield(50u);
    }
    State = UsbHostReadMmio32(Address);
    UsbHostWriteMmio32(Address, PortStatusNeutral(State) | PORT_STATUS_RESET_CHANGE |
                                    PORT_STATUS_CONNECT_CHANGE | PORT_STATUS_ENABLE_CHANGE);
    UsbHostStallYield(100000u);
    State = UsbHostReadMmio32(Address);
    return (State & PORT_STATUS_ENABLE) ? 0 : -1;
}

int UsbHostEnumerateHubOnPort(UINT32 PortIndexOneBased, UINT32 *HubAddressOut) {
    TransferRequestBlock Command;
    UINT32 CompletionCode;
    UINT32 SlotId;
    UINT32 PortSpeed;
    UINT32 State;
    UINT8 *Input;
    UINT8 *Output;
    UINT32 ContextSize;
    UINT8 Descriptor[18];
    UINTN Index;
    UINT64 Dequeue;
    UINT32 MaxPacketSize;
    char Line[40];
    char Hex[12];

    State = UsbHostRootPortStatus(PortIndexOneBased);
    PortSpeed = (State >> PORT_STATUS_SPEED_SHIFT) & 0xFu;
    if (UsbHostRootPortReset(PortIndexOneBased) != 0) {
        BoardBringUpSplashPaintLine(256, "rst fail");
        return -1;
    }

    Command.Parameter = 0;
    Command.Status = 0;
    Command.Control = TRANSFER_REQUEST_BLOCK_TYPE(TRANSFER_REQUEST_BLOCK_ENABLE_SLOT);
    if (UsbHostXhciSubmitCommand(&Command, &CompletionCode, &SlotId) != 0 ||
        CompletionCode != COMPLETION_CODE_SUCCESS || SlotId == 0) {
        BoardBringUpSplashPaintLine(256, "slot fail");
        return -1;
    }

    ContextSize = UsbHostContextSizeBytes();
    Input = UsbHostInputContextBuffer();
    Output = UsbHostDeviceContextSlot(SlotId);
    for (Index = 0; Index < ContextSize * 33u; Index++) {
        Input[Index] = 0;
    }
    for (Index = 0; Index < ContextSize * 32u; Index++) {
        Output[Index] = 0;
    }

    WriteLittleEndian32(Input + 4u, (1u << 0) | (1u << 1));
    WriteLittleEndian32(Input + ContextSize + 0u,
                        (1u << 27) | (PortSpeed << 20) | (1u << 0));
    WriteLittleEndian32(Input + ContextSize + 4u, (PortIndexOneBased & 0xFFu) << 16);

    UsbHostBindEndpointZeroRing(SlotId);
    Dequeue = UsbHostEndpointZeroRingPhysical() | 1ULL;
    MaxPacketSize = (PortSpeed >= 3u) ? 512u : ((PortSpeed == 2u) ? 64u : 8u);
    WriteLittleEndian32(Input + ContextSize * 2u + 0u, (3u << 1));
    WriteLittleEndian32(Input + ContextSize * 2u + 4u, (MaxPacketSize << 16) | 4u);
    WriteLittleEndian32(Input + ContextSize * 2u + 8u, (UINT32)Dequeue);
    WriteLittleEndian32(Input + ContextSize * 2u + 12u, (UINT32)(Dequeue >> 32));

    UsbHostSetDeviceContextBase(SlotId, (UINT64)(UINTN)Output);
    UsbHostCacheCleanRange(Input, ContextSize * 33u);
    UsbHostCacheCleanRange(Output, ContextSize * 32u);

    Command.Parameter = (UINT64)(UINTN)Input;
    Command.Status = 0;
    Command.Control = TRANSFER_REQUEST_BLOCK_TYPE(TRANSFER_REQUEST_BLOCK_ADDRESS_DEVICE) |
                      (SlotId << TRANSFER_REQUEST_BLOCK_SLOT_SHIFT);
    if (UsbHostXhciSubmitCommand(&Command, &CompletionCode, 0) != 0 ||
        CompletionCode != COMPLETION_CODE_SUCCESS) {
        BoardBringUpSplashPaintLine(256, "addr fail");
        return -1;
    }

    for (Index = 0; Index < sizeof(Descriptor); Index++) {
        Descriptor[Index] = 0;
    }
    if (UsbHostXhciControlTransfer(SlotId, 0x80u, 6u, 0x0100u, 0, Descriptor, 18u) != 0) {
        BoardBringUpSplashPaintLine(256, "desc fail");
        return -1;
    }
    if (Descriptor[4] != USB_DEVICE_CLASS_HUB) {
        BoardBringUpSplashPaintLine(256, "not hub");
        return -1;
    }

    if (HubAddressOut != 0) {
        *HubAddressOut = USB_HUB_ADDRESS;
    }
    UsbHostFormatHexadecimal32(Hex, USB_HUB_ADDRESS);
    UsbHostCopyText(Line, "hub ", sizeof(Line));
    UsbHostAppendText(Line, Hex, sizeof(Line));
    BoardBringUpSplashPaintLine(256, Line);
    return 0;
}

void BoardMix2UsbHubWatch(void) {
    UINT32 PortCount;
    UINT32 PortIndex;
    UINT32 HubAddress;
    UINT32 SawConnect;
    UINT32 LastStable[9];

    /* 显示用：去掉 W1C 变更位 */
#define PORT_STATUS_STABLE_MASK                                              \
    (PORT_STATUS_CONNECT | PORT_STATUS_ENABLE | PORT_STATUS_RESET |           \
     PORT_STATUS_POWER | (0xFu << PORT_STATUS_SPEED_SHIFT))

    if (UsbHostXhciStart() != 0) {
        for (;;) {
            UsbHostStallYield(1000000u);
        }
    }

    /* 再强制一次 DFP，结果留在 TypeCLast* 供每轮刷屏（勿被 ports 盖掉） */
    (void)UsbHostTypeCForceHost();

    PortCount = UsbHostXhciMaxPorts();
    if (PortCount == 0) {
        PortCount = 1;
    }
    if (PortCount > 4u) {
        PortCount = 4u;
    }

    for (PortIndex = 1; PortIndex <= PortCount; PortIndex++) {
        (void)UsbHostRootPortPower(PortIndex);
        LastStable[PortIndex] = 0xFFFFFFFFu;
    }

    HubAddress = 0;
    SawConnect = 0;

    for (;;) {
        for (PortIndex = 1; PortIndex <= PortCount; PortIndex++) {
            UINT32 State = UsbHostRootPortStatus(PortIndex);
            UINT32 Stable = State & PORT_STATUS_STABLE_MASK;

            LastStable[PortIndex] = Stable;

            if ((Stable & PORT_STATUS_CONNECT) != 0 && HubAddress == 0) {
                if (UsbHostEnumerateHubOnPort(PortIndex, &HubAddress) == 0) {
                    SawConnect = 1;
                }
            } else if ((Stable & PORT_STATUS_CONNECT) == 0 && SawConnect != 0) {
                UINT32 AnyConnect = 0;
                UINT32 Check;
                for (Check = 1; Check <= PortCount; Check++) {
                    if ((LastStable[Check] & PORT_STATUS_CONNECT) != 0) {
                        AnyConnect = 1;
                        break;
                    }
                }
                if (AnyConnect == 0) {
                    HubAddress = 0;
                    SawConnect = 0;
                }
            }
        }

        UsbHostPaintHubStatus(PortCount, LastStable);
        UsbHostStallYield(1000000u);
    }
#undef PORT_STATUS_STABLE_MASK
}

#else

UINT32 UsbHostRootPortStatus(UINT32 PortIndexOneBased) {
    (void)PortIndexOneBased;
    return 0;
}

int UsbHostRootPortPower(UINT32 PortIndexOneBased) {
    (void)PortIndexOneBased;
    return -1;
}

int UsbHostRootPortReset(UINT32 PortIndexOneBased) {
    (void)PortIndexOneBased;
    return -1;
}

int UsbHostEnumerateHubOnPort(UINT32 PortIndexOneBased, UINT32 *HubAddressOut) {
    (void)PortIndexOneBased;
    (void)HubAddressOut;
    return -1;
}

void BoardMix2UsbHubWatch(void) {
}

#endif
