/*
 * UsbHost/HubChildAddress.c — 子口 EnableSlot + Address Device
 *
 * 对表 X64 Device.c：FS/LS 一步 BSR=0；HS 先 BSR1 再 BSR0；SS 同 §4.6.5。
 * 真机：BSR1→addr0 邮包→BSR0 后凡邮包 USBTE（嵌套键 37a4→381044）——FS 禁止该序。
 * TT 仅 Speed<3；Route=口号。
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

UINT32 UsbHostChildSlotSpeed(UINT32 HubPortStatus) {
    if ((HubPortStatus & HUB_PORT_HIGH_SPEED) != 0) {
        return 3u;
    }
    if ((HubPortStatus & HUB_PORT_LOW_SPEED) != 0) {
        return 2u;
    }
    return 1u;
}

static UINT32 EndpointZeroMaxPacketSize(UINT32 PortSpeed) {
    /* 对表 HubEnumerate：HS/FS 初值 64；LS=8。FS 用 8 真机嵌套口 Babble（341034）。 */
    if (PortSpeed >= 4u) {
        return 512u;
    }
    if (PortSpeed == 3u || PortSpeed == 1u) {
        return 64u;
    }
    return 8u;
}

static void FillEndpointZeroInput(UINT8 *Input, UINT32 ContextSize, UINT32 SlotId,
                                  UINT32 MaxPacketSize) {
    UINT64 Dequeue;

    UsbHostBindEndpointZeroRing(SlotId);
    Dequeue = UsbHostEndpointZeroRingPhysical(SlotId) | 1ULL;
    WriteLittleEndian32(Input + ContextSize * 2u + 4u,
                        (3u << 1) | (4u << 3) | (MaxPacketSize << 16));
    WriteLittleEndian32(Input + ContextSize * 2u + 8u, (UINT32)Dequeue);
    WriteLittleEndian32(Input + ContextSize * 2u + 12u, (UINT32)(Dequeue >> 32));
    WriteLittleEndian32(Input + ContextSize * 2u + 16u, 8u);
}

int UsbHostAddressKeyboardOnHubPort(UINT32 HubSlotId, UINT32 RootPort,
                                    UINT32 ParentRouteString, UINT32 HubPortOneBased,
                                    UINT32 DeviceSpeed, UINT32 IssueSetAddress,
                                    UINT32 *KeyboardSlotOut) {
    TransferRequestBlock Command;
    UINT32 CompletionCode;
    UINT32 SlotId;
    UINT8 *Input;
    UINT8 *Output;
    UINT32 ContextSize;
    UINTN Index;
    UINT32 MaxPacketSize;
    UINT32 RouteString;
    UINT32 RouteShift;

    /* 下一级 nibble：ParentRoute 已占低位则左移 4（二层 hub） */
    RouteShift = 0;
    while (RouteShift < 20u &&
           ((ParentRouteString >> RouteShift) & 0xFu) != 0u) {
        RouteShift += 4u;
    }
    RouteString =
        (ParentRouteString & 0xFFFFFu) | (((HubPortOneBased & 0xFu) << RouteShift));

    Command.Parameter = 0;
    Command.Status = 0;
    Command.Control = TRANSFER_REQUEST_BLOCK_TYPE(TRANSFER_REQUEST_BLOCK_ENABLE_SLOT) |
                      TRANSFER_REQUEST_BLOCK_INTERRUPT_ON_COMPLETION |
                      ((UsbHostXhciUsb2ProtocolSlotType() & 0x1Fu) << 16);
    if (UsbHostXhciSubmitCommand(&Command, &CompletionCode, &SlotId) != 0 ||
        CompletionCode != COMPLETION_CODE_SUCCESS || SlotId == 0) {
        UsbHostKeyboardStatusCodeSet(0x200u | (CompletionCode & 0xFFu));
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
                        (1u << 27) | (DeviceSpeed << 20) | RouteString);
    WriteLittleEndian32(Input + ContextSize + 4u, (RootPort & 0xFFu) << 16);
    if (DeviceSpeed < 3u) {
        WriteLittleEndian32(Input + ContextSize + 8u,
                            HubSlotId | ((HubPortOneBased & 0xFFu) << 8));
    }

    MaxPacketSize = EndpointZeroMaxPacketSize(DeviceSpeed);
    FillEndpointZeroInput(Input, ContextSize, SlotId, MaxPacketSize);

    UsbHostSetDeviceContextBase(SlotId, (UINT64)(UINTN)Output);
    UsbHostCacheCleanRange(Input, ContextSize * 33u);
    UsbHostCacheCleanRange(Output, ContextSize * 32u);

    /*
     * FS/LS：一步 BSR=0（对表 X64；初值 MPS 仍 64，避免旧 341034 Babble）。
     * HS：BSR1→BSR0（现网一层 hub/MSC）。SS：同 §4.6.5 BSR1→BSR0。
     */
    Command.Parameter = (UINT64)(UINTN)Input;
    Command.Status = 0;
    if (DeviceSpeed < 3u) {
        Command.Control =
            TRANSFER_REQUEST_BLOCK_TYPE(TRANSFER_REQUEST_BLOCK_ADDRESS_DEVICE) |
            TRANSFER_REQUEST_BLOCK_INTERRUPT_ON_COMPLETION |
            (SlotId << TRANSFER_REQUEST_BLOCK_SLOT_SHIFT);
        if (UsbHostXhciSubmitCommand(&Command, &CompletionCode, 0) != 0 ||
            CompletionCode != COMPLETION_CODE_SUCCESS) {
            (void)UsbHostXhciDisableSlot(SlotId);
            UsbHostKeyboardStatusCodeSet(0x300u | (CompletionCode & 0xFFu));
            return -1;
        }
    } else {
        Command.Control =
            TRANSFER_REQUEST_BLOCK_TYPE(TRANSFER_REQUEST_BLOCK_ADDRESS_DEVICE) |
            TRANSFER_REQUEST_BLOCK_BLOCK_SET_ADDRESS_REQUEST |
            TRANSFER_REQUEST_BLOCK_INTERRUPT_ON_COMPLETION |
            (SlotId << TRANSFER_REQUEST_BLOCK_SLOT_SHIFT);
        if (UsbHostXhciSubmitCommand(&Command, &CompletionCode, 0) != 0 ||
            CompletionCode != COMPLETION_CODE_SUCCESS) {
            (void)UsbHostXhciDisableSlot(SlotId);
            UsbHostKeyboardStatusCodeSet(0x310u | (CompletionCode & 0xFFu));
            return -1;
        }
        if (IssueSetAddress != 0) {
            UsbHostCacheCleanRange(Input, ContextSize * 33u);
            UsbHostCacheCleanRange(Output, ContextSize * 32u);
            Command.Parameter = (UINT64)(UINTN)Input;
            Command.Status = 0;
            Command.Control =
                TRANSFER_REQUEST_BLOCK_TYPE(TRANSFER_REQUEST_BLOCK_ADDRESS_DEVICE) |
                TRANSFER_REQUEST_BLOCK_INTERRUPT_ON_COMPLETION |
                (SlotId << TRANSFER_REQUEST_BLOCK_SLOT_SHIFT);
            if (UsbHostXhciSubmitCommand(&Command, &CompletionCode, 0) != 0 ||
                CompletionCode != COMPLETION_CODE_SUCCESS) {
                (void)UsbHostXhciDisableSlot(SlotId);
                UsbHostKeyboardStatusCodeSet(0x300u | (CompletionCode & 0xFFu));
                return -1;
            }
        }
    }
    if (KeyboardSlotOut != 0) {
        *KeyboardSlotOut = SlotId;
    }
    return 0;
}

int UsbHostChildSetAddress(UINT32 SlotId, UINT32 DeviceSpeed,
                           UINT32 EndpointZeroMaxPacket) {
    TransferRequestBlock Command;
    UINT32 CompletionCode;
    UINT8 *Input;
    UINT8 *Output;
    UINT32 ContextSize;
    UINTN Index;
    UINT32 MaxPacketSize;
    UINT32 EndpointWord;

    if (SlotId == 0) {
        return -1;
    }
    /*
     * 对表嵌套 hub：BSR0 勿 Sync/重绑 EP0（真机 Sync 后 27Cx→2C… USBTE）。
     * 读 Output 前 Invalidate；只改 EP0 MPS，保留 HC 当前 dequeue。
     */
    (void)DeviceSpeed;
    ContextSize = UsbHostContextSizeBytes();
    Input = UsbHostInputContextBuffer();
    Output = UsbHostDeviceContextSlot(SlotId);
    UsbHostCacheCleanInvalidateRange(Output, ContextSize * 32u);
    for (Index = 0; Index < ContextSize * 33u; Index++) {
        Input[Index] = 0;
    }
    WriteLittleEndian32(Input + 4u, (1u << 0) | (1u << 1));
    for (Index = 0; Index < ContextSize * 2u; Index++) {
        Input[ContextSize + Index] = Output[Index];
    }
    MaxPacketSize = EndpointZeroMaxPacket;
    if (MaxPacketSize < 8u) {
        MaxPacketSize = 8u;
    }
    if (MaxPacketSize > 64u) {
        MaxPacketSize = 64u;
    }
    EndpointWord = (3u << 1) | (4u << 3) | (MaxPacketSize << 16);
    WriteLittleEndian32(Input + ContextSize * 2u + 4u, EndpointWord);
    UsbHostCacheCleanRange(Input, ContextSize * 33u);
    UsbHostCacheCleanRange(Output, ContextSize * 32u);

    Command.Parameter = (UINT64)(UINTN)Input;
    Command.Status = 0;
    Command.Control = TRANSFER_REQUEST_BLOCK_TYPE(TRANSFER_REQUEST_BLOCK_ADDRESS_DEVICE) |
                      TRANSFER_REQUEST_BLOCK_INTERRUPT_ON_COMPLETION |
                      (SlotId << TRANSFER_REQUEST_BLOCK_SLOT_SHIFT);
    if (UsbHostXhciSubmitCommand(&Command, &CompletionCode, 0) != 0 ||
        CompletionCode != COMPLETION_CODE_SUCCESS) {
        UsbHostKeyboardStatusCodeSet(0x300u | (CompletionCode & 0xFFu));
        return -1;
    }
    return 0;
}

#else

UINT32 UsbHostChildSlotSpeed(UINT32 HubPortStatus) {
    (void)HubPortStatus;
    return 0;
}

int UsbHostAddressKeyboardOnHubPort(UINT32 HubSlotId, UINT32 RootPort,
                                    UINT32 ParentRouteString, UINT32 HubPortOneBased,
                                    UINT32 DeviceSpeed, UINT32 IssueSetAddress,
                                    UINT32 *KeyboardSlotOut) {
    (void)HubSlotId;
    (void)RootPort;
    (void)ParentRouteString;
    (void)HubPortOneBased;
    (void)DeviceSpeed;
    (void)IssueSetAddress;
    (void)KeyboardSlotOut;
    return -1;
}

int UsbHostChildSetAddress(UINT32 SlotId, UINT32 DeviceSpeed,
                           UINT32 EndpointZeroMaxPacket) {
    (void)SlotId;
    (void)DeviceSpeed;
    (void)EndpointZeroMaxPacket;
    return -1;
}

#endif
