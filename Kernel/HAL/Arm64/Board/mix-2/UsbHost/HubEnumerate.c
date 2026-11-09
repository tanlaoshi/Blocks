/*
 * UsbHost/HubEnumerate.c — hub EnableSlot / Address / GetDescriptor
 *
 * 对表 xHCI 1.1；EP0 MPS：HS/FS=64。根口复位见 HubRootPort（leavePRC）。
 */
#include "UsbHost/Private.h"

#ifndef BOARD_MIX2_USB_HOST_BRINGUP
#define BOARD_MIX2_USB_HOST_BRINGUP 0
#endif

#if BOARD_MIX2_USB_HOST_BRINGUP

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
static UINT32 sHubSlotId;
static UINT32 sHubRootPortIndex;
static UINT32 sHubPortSpeed;

UINT32 UsbHostEnumerateStatusCode(void) {
    return sEnumerateStatusCode;
}

void UsbHostEnumerateStatusCodeClear(void) {
    sEnumerateStatusCode = 0;
    sHubSlotId = 0;
    sHubRootPortIndex = 0;
    sHubPortSpeed = 0;
}

void UsbHostEnumerateStatusCodeSet(UINT32 Code) {
    sEnumerateStatusCode = Code;
}

UINT32 UsbHostHubSlotId(void) {
    return sHubSlotId;
}

UINT32 UsbHostHubRootPortIndex(void) {
    return sHubRootPortIndex;
}

UINT32 UsbHostHubPortSpeed(void) {
    return sHubPortSpeed;
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
    State = UsbHostRootPortStatus(PortIndexOneBased);
    PortSpeed = (State >> PORT_STATUS_SPEED_SHIFT) & 0xFu;

    Command.Parameter = 0;
    Command.Status = 0;
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
     * 对表 X64/Pixel 根口 RouteString=0；每槽 EP0；Address 直接 BSR0
     *（MIX2 USB-2 真机 e a1 路径）。
     */
    UsbHostSetDeviceContextBase(SlotId, (UINT64)(UINTN)Output);
    WriteLittleEndian32(Input + 4u, (1u << 0) | (1u << 1));
    WriteLittleEndian32(Input + ContextSize + 0u,
                        (1u << 27) | (PortSpeed << 20));
    WriteLittleEndian32(Input + ContextSize + 4u, (PortIndexOneBased & 0xFFu) << 16);

    UsbHostBindEndpointZeroRing(SlotId);
    Dequeue = UsbHostEndpointZeroRingPhysical(SlotId) | 1ULL;
    MaxPacketSize = EndpointZeroMaxPacketSize(PortSpeed);
    WriteLittleEndian32(Input + ContextSize * 2u + 4u,
                        (3u << 1) | (4u << 3) | (MaxPacketSize << 16));
    WriteLittleEndian32(Input + ContextSize * 2u + 8u, (UINT32)Dequeue);
    WriteLittleEndian32(Input + ContextSize * 2u + 12u, (UINT32)(Dequeue >> 32));
    WriteLittleEndian32(Input + ContextSize * 2u + 16u, 8u);

    UsbHostCacheCleanRange(Input, ContextSize * 33u);
    UsbHostCacheCleanRange(Output, ContextSize * 32u);

    Command.Parameter = (UINT64)(UINTN)Input;
    Command.Status = 0;
    Command.Control = TRANSFER_REQUEST_BLOCK_TYPE(TRANSFER_REQUEST_BLOCK_ADDRESS_DEVICE) |
                      TRANSFER_REQUEST_BLOCK_INTERRUPT_ON_COMPLETION |
                      (SlotId << TRANSFER_REQUEST_BLOCK_SLOT_SHIFT);
    if (UsbHostXhciSubmitCommand(&Command, &CompletionCode, 0) != 0 ||
        CompletionCode != COMPLETION_CODE_SUCCESS) {
        sEnumerateStatusCode = 0x300u | (CompletionCode & 0xFFu);
        (void)UsbHostXhciDisableSlot(SlotId);
        return -1;
    }

    for (Index = 0; Index < sizeof(Descriptor); Index++) {
        Descriptor[Index] = 0;
    }
    if (UsbHostXhciControlTransfer(SlotId, 0x80u, 6u, 0x0100u, 0, Descriptor, 18u) !=
        0) {
        sEnumerateStatusCode = 0x40u;
        (void)UsbHostXhciDisableSlot(SlotId);
        return -1;
    }
    if (Descriptor[4] != USB_DEVICE_CLASS_HUB) {
        sEnumerateStatusCode = 0x50u | (Descriptor[4] & 0x0Fu);
        (void)UsbHostXhciDisableSlot(SlotId);
        return -1;
    }

    sHubSlotId = SlotId;
    sHubRootPortIndex = PortIndexOneBased;
    sHubPortSpeed = PortSpeed;
    if (HubAddressOut != 0) {
        *HubAddressOut = USB_HUB_ADDRESS;
    }
    sEnumerateStatusCode = 0xA0u | (USB_HUB_ADDRESS & 0x0Fu);
    return 0;
}

#else

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

void UsbHostEnumerateStatusCodeSet(UINT32 Code) {
    (void)Code;
}

UINT32 UsbHostHubSlotId(void) {
    return 0;
}

UINT32 UsbHostHubRootPortIndex(void) {
    return 0;
}

UINT32 UsbHostHubPortSpeed(void) {
    return 0;
}

#endif
