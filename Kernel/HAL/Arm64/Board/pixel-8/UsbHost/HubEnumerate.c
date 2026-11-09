/*
 * UsbHost/HubEnumerate.c — 根口电源/复位 + hub 枚举（自 HubWatch 拆出）
 *
 * 对表 xHCI 1.1 PORTSC；EP0 MPS：HS/FS=64（禁把 HS 当 SS→512）。
 */
#include "UsbHost/Private.h"

#ifndef BOARD_PIXEL8_USB_HOST_BRINGUP
#define BOARD_PIXEL8_USB_HOST_BRINGUP 0
#endif

#if BOARD_PIXEL8_USB_HOST_BRINGUP

static UINT32 PortStatusNeutral(UINT32 State) {
    return (State & PORT_STATUS_READ_ONLY) | (State & PORT_STATUS_READ_WRITE_SET);
}

static void WriteLittleEndian32(UINT8 *Destination, UINT32 Value) {
    Destination[0] = (UINT8)(Value & 0xFFu);
    Destination[1] = (UINT8)((Value >> 8) & 0xFFu);
    Destination[2] = (UINT8)((Value >> 16) & 0xFFu);
    Destination[3] = (UINT8)((Value >> 24) & 0xFFu);
}

static UINT32 EndpointZeroMaxPacketSize(UINT32 PortSpeed) {
    if (PortSpeed >= 4u) {
        return 512u;
    }
    if (PortSpeed == 3u || PortSpeed == 1u) {
        return 64u;
    }
    return 8u;
}

static UINT32 sEnumerateStatusCode;

UINT32 UsbHostEnumerateStatusCode(void) {
    return sEnumerateStatusCode;
}

void UsbHostEnumerateStatusCodeClear(void) {
    sEnumerateStatusCode = 0;
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

static void ConfigureUsb2PhyForHostChirp(void) {
    UINT32 Usb2PhyConfiguration;

    Usb2PhyConfiguration =
        UsbHostReadMmio32(DWC3_REGISTER_BASE + DWC3_GUSB2PHYCFG_OFFSET);
    Usb2PhyConfiguration &=
        ~(DWC3_PHY_SOFTWARE_RESET | DWC3_GUSB2PHYCFG_ULPI_UTMI |
          DWC3_GUSB2PHYCFG_SUSPEND_USB20 | DWC3_GUSB2PHYCFG_ENABLE_SLEEP_MODE |
          DWC3_GUSB2PHYCFG_U2_FREE_CLOCK_EXISTS |
          (1u << DWC3_GUSB2PHYCFG_PHYSICAL_INTERFACE_SHIFT) |
          DWC3_GUSB2PHYCFG_USB_TURNAROUND_MASK);
    Usb2PhyConfiguration |= (DWC3_GUSB2PHYCFG_UTMI_TURNAROUND_8BIT
                             << DWC3_GUSB2PHYCFG_USB_TURNAROUND_SHIFT);
    UsbHostWriteMmio32(DWC3_REGISTER_BASE + DWC3_GUSB2PHYCFG_OFFSET,
                       Usb2PhyConfiguration);
}

/*
 * 对表 xHCI USB2 + X64 Port.c 真机：
 *   PED=0 时清 sticky → PR → 等 PRC → 等 PED；
 *   **成功后 leavePRC**（PED 后再 W1C CHANGE → 真机打回 Polling / Address USBTE）；
 *   再等 USB2 T_RSTRCY（≥10ms；X64 用 50ms）才允许 Address。
 * 禁止 USB2 写 PLS+LWS；禁止复位路径再 CLOCK_DISABLE。
 */
int UsbHostRootPortReset(UINT32 PortIndexOneBased) {
    UINT64 Address = UsbHostXhciPortStatusAddress(PortIndexOneBased);
    UINT32 State;
    UINTN Guard;
    UINT32 SawResetChange;

    ConfigureUsb2PhyForHostChirp();
    UsbHostStallYield(200000u);

    State = UsbHostReadMmio32(Address);
    if ((State & PORT_STATUS_CONNECT) == 0) {
        return -1;
    }
    /* 仅 PED=0 时清变更（对表 X64 ResetPort.clrSticky） */
    if ((State & PORT_STATUS_ENABLE) == 0 &&
        (State & PORT_STATUS_CHANGE_BITS) != 0) {
        UsbHostWriteMmio32(Address, PortStatusNeutral(State) | PORT_STATUS_CHANGE_BITS);
        UsbHostStallYield(50000u);
    }

    State = UsbHostReadMmio32(Address);
    UsbHostWriteMmio32(Address, PortStatusNeutral(State) | PORT_STATUS_POWER |
                                    PORT_STATUS_RESET);

    for (Guard = 0; Guard < 2000000u; Guard++) {
        State = UsbHostReadMmio32(Address);
        if ((State & PORT_STATUS_RESET) == 0) {
            break;
        }
        UsbHostStallYield(50u);
    }
    if ((State & PORT_STATUS_RESET) != 0) {
        return -1;
    }

    SawResetChange = 0;
    for (Guard = 0; Guard < 200000u; Guard++) {
        State = UsbHostReadMmio32(Address);
        if ((State & PORT_STATUS_RESET_CHANGE) != 0) {
            SawResetChange = 1;
            break;
        }
        UsbHostStallYield(50u);
    }
    if (SawResetChange == 0) {
        return -1;
    }

    /* 勿在此处 W1C CHANGE：leavePRC（X64 真机血泪） */
    for (Guard = 0; Guard < 400u; Guard++) {
        State = UsbHostReadMmio32(Address);
        if ((State & PORT_STATUS_CONNECT) == 0) {
            return -1;
        }
        if ((State & PORT_STATUS_ENABLE) != 0) {
            /* USB2 复位恢复；对表 X64 StallMilliseconds(50) */
            UsbHostStallYield(2000000u);
            return 0;
        }
        UsbHostStallYield(5000u);
    }
    return -1;
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

    sEnumerateStatusCode = 1u;
    if (UsbHostRootPortReset(PortIndexOneBased) != 0) {
        State = UsbHostRootPortStatus(PortIndexOneBased);
        sEnumerateStatusCode = 0x1000u | (State & 0xFFu);
        return -1;
    }
    /* 速度以复位后 PORTSC 为准（xHCI：复位前 Speed 未定义） */
    State = UsbHostRootPortStatus(PortIndexOneBased);
    PortSpeed = (State >> PORT_STATUS_SPEED_SHIFT) & 0xFu;

    Command.Parameter = 0;
    Command.Status = 0;
    /* xHCI §4.6.3：双协议 HC 须填 Protocol Slot Type（USB2 口） */
    Command.Control = TRANSFER_REQUEST_BLOCK_TYPE(TRANSFER_REQUEST_BLOCK_ENABLE_SLOT) |
                      ((UsbHostXhciUsb2ProtocolSlotType() & 0x1Fu) << 16);
    if (UsbHostXhciSubmitCommand(&Command, &CompletionCode, &SlotId) != 0 ||
        CompletionCode != COMPLETION_CODE_SUCCESS || SlotId == 0) {
        sEnumerateStatusCode = 0x200u | (CompletionCode & 0xFFu);
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

    /*
     * 对表 X64 AddressDeviceOnPort（根口 RouteString=0）：
     * 禁把口号写进 Route String（曾 |1 → Address USBTE）。
     * EP0 dword1：CErr|Type=Control(4)|MPS（同 Device.c）。
     */
    WriteLittleEndian32(Input + 4u, (1u << 0) | (1u << 1));
    WriteLittleEndian32(Input + ContextSize + 0u,
                        (1u << 27) | (PortSpeed << 20));
    WriteLittleEndian32(Input + ContextSize + 4u, (PortIndexOneBased & 0xFFu) << 16);

    UsbHostBindEndpointZeroRing(SlotId);
    Dequeue = UsbHostEndpointZeroRingPhysical() | 1ULL;
    MaxPacketSize = EndpointZeroMaxPacketSize(PortSpeed);
    WriteLittleEndian32(Input + ContextSize * 2u + 4u,
                        (3u << 1) | (4u << 3) | (MaxPacketSize << 16));
    WriteLittleEndian32(Input + ContextSize * 2u + 8u, (UINT32)Dequeue);
    WriteLittleEndian32(Input + ContextSize * 2u + 12u, (UINT32)(Dequeue >> 32));
    WriteLittleEndian32(Input + ContextSize * 2u + 16u, 8u);

    UsbHostSetDeviceContextBase(SlotId, (UINT64)(UINTN)Output);
    UsbHostCacheCleanRange(Input, ContextSize * 33u);
    UsbHostCacheCleanRange(Output, ContextSize * 32u);

    Command.Parameter = (UINT64)(UINTN)Input;
    Command.Status = 0;
    Command.Control = TRANSFER_REQUEST_BLOCK_TYPE(TRANSFER_REQUEST_BLOCK_ADDRESS_DEVICE) |
                      (SlotId << TRANSFER_REQUEST_BLOCK_SLOT_SHIFT);
    if (UsbHostXhciSubmitCommand(&Command, &CompletionCode, 0) != 0 ||
        CompletionCode != COMPLETION_CODE_SUCCESS) {
        sEnumerateStatusCode = 0x300u | (CompletionCode & 0xFFu);
        return -1;
    }

    for (Index = 0; Index < sizeof(Descriptor); Index++) {
        Descriptor[Index] = 0;
    }
    if (UsbHostXhciControlTransfer(SlotId, 0x80u, 6u, 0x0100u, 0, Descriptor, 18u) !=
        0) {
        sEnumerateStatusCode = 0x40u;
        return -1;
    }
    if (Descriptor[4] != USB_DEVICE_CLASS_HUB) {
        sEnumerateStatusCode = 0x50u | (Descriptor[4] & 0x0Fu);
        return -1;
    }

    if (HubAddressOut != 0) {
        *HubAddressOut = USB_HUB_ADDRESS;
    }
    sEnumerateStatusCode = 0xA0u | (USB_HUB_ADDRESS & 0x0Fu);
    return 0;
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

UINT32 UsbHostEnumerateStatusCode(void) {
    return 0;
}

void UsbHostEnumerateStatusCodeClear(void) {
}

#endif
