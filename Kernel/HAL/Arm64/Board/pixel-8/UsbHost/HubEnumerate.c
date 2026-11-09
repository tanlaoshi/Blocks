/*
 * UsbHost/HubEnumerate.c — hub EnableSlot / Address / GetDescriptor
 *
 * 对表 xHCI 1.1；EP0 MPS：HS/FS=64（禁把 HS 当 SS→512）。
 * Address：BSR1→BSR0 分诊上下文 vs 线上 SET_ADDRESS。
 */
#include "UsbHost/Private.h"

#ifndef BOARD_PIXEL8_USB_HOST_BRINGUP
#define BOARD_PIXEL8_USB_HOST_BRINGUP 0
#endif

#if BOARD_PIXEL8_USB_HOST_BRINGUP

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

void UsbHostEnumerateStatusCodeSet(UINT32 Code) {
    sEnumerateStatusCode = Code;
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
        if (sEnumerateStatusCode < 0x1000u) {
            State = UsbHostRootPortStatus(PortIndexOneBased);
            sEnumerateStatusCode = 0x1000u | (State & 0xFFu);
        }
        return -1;
    }
    State = UsbHostRootPortStatus(PortIndexOneBased);
    PortSpeed = (State >> PORT_STATUS_SPEED_SHIFT) & 0xFu;
    /* Address 前再确认 PED（串口曾见 enum 瞬间 601） */
    if ((State & PORT_STATUS_ENABLE) == 0 || PortSpeed == 0u) {
        sEnumerateStatusCode = 0x1800u | (State & 0xFFu);
        return -1;
    }

    /*
     * HS 空闲＝SE0（UTMI ls=0）。屏准曾见 PED=HS 却 ls1＝J → SET_ADDRESS USBTE。
     * 对表 USB2.0 HS idle：Address 前等 dig SE0（非启发式缩超时）。
     */
    if (PortSpeed >= 3u) {
        UINTN Settle;
        UsbHostPhysicalRefreshCommonControl();
        for (Settle = 0; Settle < 50u; Settle++) {
            if (UsbHostPhysicalLineState() == 0u) {
                break;
            }
            UsbHostStallMilliseconds(1u);
            UsbHostPhysicalRefreshCommonControl();
        }
        if (UsbHostPhysicalLineState() != 0u) {
            sEnumerateStatusCode =
                0x1900u | ((UsbHostPhysicalLineState() & 0x3u) << 4) |
                (State & 0xFu);
            return -1;
        }
    }

    CompletionCode = 0;
    Command.Parameter = 0;
    Command.Status = 0;
    Command.Control = TRANSFER_REQUEST_BLOCK_TYPE(TRANSFER_REQUEST_BLOCK_ENABLE_SLOT) |
                      TRANSFER_REQUEST_BLOCK_INTERRUPT_ON_COMPLETION |
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
     * 对表 Mix2/X64 根口：RouteString=0；先 DCBAA；EP0 CErr|Type4|MPS。
     * Address：BSR=1（无 USB）再 BSR=0（SET_ADDRESS）。
     * e31x＝上下文/DMA；e30x＝线上 SET_ADDRESS。
     */
    UsbHostSetDeviceContextBase(SlotId, (UINT64)(UINTN)Output);
    WriteLittleEndian32(Input + 4u, (1u << 0) | (1u << 1));
    WriteLittleEndian32(Input + ContextSize + 0u,
                        (1u << 27) | (PortSpeed << 20));
    WriteLittleEndian32(Input + ContextSize + 4u,
                        (PortIndexOneBased & 0xFFu) << 16);

    UsbHostBindEndpointZeroRing(SlotId);
    Dequeue = UsbHostEndpointZeroRingPhysical() | 1ULL;
    MaxPacketSize = EndpointZeroMaxPacketSize(PortSpeed);
    WriteLittleEndian32(Input + ContextSize * 2u + 4u,
                        (3u << 1) | (4u << 3) | (MaxPacketSize << 16));
    WriteLittleEndian32(Input + ContextSize * 2u + 8u, (UINT32)Dequeue);
    WriteLittleEndian32(Input + ContextSize * 2u + 12u, (UINT32)(Dequeue >> 32));
    WriteLittleEndian32(Input + ContextSize * 2u + 16u, 8u);

    UsbHostCacheCleanRange(Input, ContextSize * 33u);
    UsbHostCacheCleanRange(Output, ContextSize * 32u);

    CompletionCode = 0;
    Command.Parameter = (UINT64)(UINTN)Input;
    Command.Status = 0;
    Command.Control = TRANSFER_REQUEST_BLOCK_TYPE(TRANSFER_REQUEST_BLOCK_ADDRESS_DEVICE) |
                      TRANSFER_REQUEST_BLOCK_BLOCK_SET_ADDRESS_REQUEST |
                      TRANSFER_REQUEST_BLOCK_INTERRUPT_ON_COMPLETION |
                      (SlotId << TRANSFER_REQUEST_BLOCK_SLOT_SHIFT);
    if (UsbHostXhciSubmitCommand(&Command, &CompletionCode, 0) != 0 ||
        CompletionCode != COMPLETION_CODE_SUCCESS) {
        sEnumerateStatusCode = 0x310u | (CompletionCode & 0xFFu);
        return -1;
    }

    UsbHostCacheCleanRange(Input, ContextSize * 33u);
    UsbHostCacheCleanRange(Output, ContextSize * 32u);
    CompletionCode = 0;
    Command.Parameter = (UINT64)(UINTN)Input;
    Command.Status = 0;
    Command.Control = TRANSFER_REQUEST_BLOCK_TYPE(TRANSFER_REQUEST_BLOCK_ADDRESS_DEVICE) |
                      TRANSFER_REQUEST_BLOCK_INTERRUPT_ON_COMPLETION |
                      (SlotId << TRANSFER_REQUEST_BLOCK_SLOT_SHIFT);
    if (UsbHostXhciSubmitCommand(&Command, &CompletionCode, 0) != 0 ||
        CompletionCode != COMPLETION_CODE_SUCCESS) {
        /* e3ls cc：bits15..8＝Address 瞬间 dig ls（1＝J），低字节＝完成码 */
        UsbHostPhysicalRefreshCommonControl();
        sEnumerateStatusCode =
            0x300u | ((UsbHostPhysicalLineState() & 0x3u) << 8) |
            (CompletionCode & 0xFFu);
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

#endif
