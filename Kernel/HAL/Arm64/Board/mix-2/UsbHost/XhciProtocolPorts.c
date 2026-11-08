/*
 * UsbHost/XhciProtocolPorts.c — xHCI Supported Protocol 扩展能力 → USB2 口位图
 *
 * 对表 xHCI 1.1 §7.2：Major=2 的 Compatible Port Offset/Count（1-based）。
 * Next 指针：相对当前能力的 dword 偏移（0=结束）。
 */
#include "UsbHost/Private.h"

#ifndef BOARD_MIX2_USB_HOST_BRINGUP
#define BOARD_MIX2_USB_HOST_BRINGUP 0
#endif

#if BOARD_MIX2_USB_HOST_BRINGUP

#define XHCI_HCCPARAMS1_OFFSET                 0x10u
#define XHCI_EXTENDED_CAPABILITY_POINTER_SHIFT 16
#define XHCI_CAPABILITY_IDENTIFIER_MASK        0xFFu
#define XHCI_CAPABILITY_NEXT_SHIFT             8
#define XHCI_CAPABILITY_NEXT_MASK              0xFFu
#define XHCI_PROTOCOL_CAPABILITY_IDENTIFIER    2u
#define XHCI_PROTOCOL_MAJOR_SHIFT              24
#define XHCI_PROTOCOL_MAJOR_MASK               0xFFu
#define XHCI_PROTOCOL_PORT_OFFSET_MASK         0xFFu
#define XHCI_PROTOCOL_PORT_COUNT_SHIFT         8
#define XHCI_PROTOCOL_PORT_COUNT_MASK          0xFFu

#define XHCI_PROTOCOL_SLOT_TYPE_MASK 0x1Fu

static UINT32 sUsb2PortBitMask;
static UINT32 sUsb3PortBitMask;
static UINT32 sUsb2ProtocolSlotType;

UINT32 UsbHostXhciUsb2PortBitMask(void) {
    return sUsb2PortBitMask;
}

UINT32 UsbHostXhciUsb3PortBitMask(void) {
    return sUsb3PortBitMask;
}

UINT32 UsbHostXhciUsb2ProtocolSlotType(void) {
    return sUsb2ProtocolSlotType;
}

int UsbHostXhciProtocolPortsProbe(UINT64 CapabilityBase, UINT32 MaxPorts) {
    UINT32 HostControllerCapabilityParameters1;
    UINT32 ExtendedOffsetDwords;
    UINT32 Guard;
    UINT32 PortIndex;

    sUsb2PortBitMask = 0;
    sUsb3PortBitMask = 0;
    sUsb2ProtocolSlotType = 0;
    if (MaxPorts == 0 || MaxPorts > 31u) {
        return -1;
    }

    HostControllerCapabilityParameters1 =
        UsbHostReadMmio32(CapabilityBase + XHCI_HCCPARAMS1_OFFSET);
    ExtendedOffsetDwords = HostControllerCapabilityParameters1 >>
                           XHCI_EXTENDED_CAPABILITY_POINTER_SHIFT;
    if (ExtendedOffsetDwords == 0) {
        for (PortIndex = 1; PortIndex <= MaxPorts; PortIndex++) {
            sUsb2PortBitMask |= (1u << PortIndex);
        }
        return 0;
    }

    for (Guard = 0; Guard < 64u && ExtendedOffsetDwords != 0; Guard++) {
        UINT64 CapabilityAddress =
            CapabilityBase + ((UINT64)ExtendedOffsetDwords * 4u);
        UINT32 Header = UsbHostReadMmio32(CapabilityAddress);
        UINT32 Identifier = Header & XHCI_CAPABILITY_IDENTIFIER_MASK;
        UINT32 NextDwords =
            (Header >> XHCI_CAPABILITY_NEXT_SHIFT) & XHCI_CAPABILITY_NEXT_MASK;
        UINT32 MajorRevision =
            (Header >> XHCI_PROTOCOL_MAJOR_SHIFT) & XHCI_PROTOCOL_MAJOR_MASK;

        if (Identifier == XHCI_PROTOCOL_CAPABILITY_IDENTIFIER) {
            UINT32 PortWord = UsbHostReadMmio32(CapabilityAddress + 8u);
            UINT32 SlotTypeWord = UsbHostReadMmio32(CapabilityAddress + 12u);
            UINT32 PortOffset = PortWord & XHCI_PROTOCOL_PORT_OFFSET_MASK;
            UINT32 PortCount = (PortWord >> XHCI_PROTOCOL_PORT_COUNT_SHIFT) &
                               XHCI_PROTOCOL_PORT_COUNT_MASK;
            UINT32 ProtocolSlotType = SlotTypeWord & XHCI_PROTOCOL_SLOT_TYPE_MASK;
            UINT32 EndPort;

            if (PortOffset != 0 && PortCount != 0) {
                EndPort = PortOffset + PortCount - 1u;
                if (EndPort > MaxPorts) {
                    EndPort = MaxPorts;
                }
                for (PortIndex = PortOffset; PortIndex <= EndPort;
                     PortIndex++) {
                    if (MajorRevision == 2u) {
                        sUsb2PortBitMask |= (1u << PortIndex);
                        sUsb2ProtocolSlotType = ProtocolSlotType;
                    } else if (MajorRevision == 3u) {
                        sUsb3PortBitMask |= (1u << PortIndex);
                    }
                }
            }
        }

        if (NextDwords == 0) {
            break;
        }
        ExtendedOffsetDwords += NextDwords;
    }

    if (sUsb2PortBitMask == 0) {
        for (PortIndex = 1; PortIndex <= MaxPorts; PortIndex++) {
            sUsb2PortBitMask |= (1u << PortIndex);
        }
    }
    return 0;
}

#else

UINT32 UsbHostXhciUsb2PortBitMask(void) {
    return 0;
}

UINT32 UsbHostXhciUsb3PortBitMask(void) {
    return 0;
}

UINT32 UsbHostXhciUsb2ProtocolSlotType(void) {
    return 0;
}

int UsbHostXhciProtocolPortsProbe(UINT64 CapabilityBase, UINT32 MaxPorts) {
    (void)CapabilityBase;
    (void)MaxPorts;
    return -1;
}

#endif
