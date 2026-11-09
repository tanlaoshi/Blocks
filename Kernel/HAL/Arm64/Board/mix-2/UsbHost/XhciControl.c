/*
 * UsbHost/XhciControl.c — EP0 Setup/Data/Status 控制传输（每槽环）
 */
#include "UsbHost/Private.h"

#ifndef BOARD_MIX2_USB_HOST_BRINGUP
#define BOARD_MIX2_USB_HOST_BRINGUP 0
#endif

#if BOARD_MIX2_USB_HOST_BRINGUP

static UINT32 sControlLastCompletionCode;

static void MemoryZero(void *Destination, UINTN Size) {
    UINT8 *Bytes = (UINT8 *)Destination;
    UINTN Index;

    for (Index = 0; Index < Size; Index++) {
        Bytes[Index] = 0;
    }
}

UINT32 UsbHostXhciControlLastCompletionCode(void) {
    return sControlLastCompletionCode;
}

int UsbHostXhciControlTransfer(UINT32 SlotId, UINT8 RequestType, UINT8 Request,
                               UINT16 Value, UINT16 Index, void *Data, UINT16 Length) {
    TransferRequestBlock Block;
    UINT32 CompletionCode = 0;
    UINT8 *SetupPacket = UsbHostSetupPacketBuffer();
    UINT8 *ControlData = UsbHostControlDataBuffer();
    int HasData = Length != 0 && Data != 0;

    sControlLastCompletionCode = 0;
    if (SlotId == 0) {
        return -1;
    }
    if (HasData && Length > USB_CONTROL_DATA_CAPACITY) {
        return -1;
    }

    SetupPacket[0] = RequestType;
    SetupPacket[1] = Request;
    SetupPacket[2] = (UINT8)(Value & 0xFFu);
    SetupPacket[3] = (UINT8)((Value >> 8) & 0xFFu);
    SetupPacket[4] = (UINT8)(Index & 0xFFu);
    SetupPacket[5] = (UINT8)((Index >> 8) & 0xFFu);
    SetupPacket[6] = (UINT8)(Length & 0xFFu);
    SetupPacket[7] = (UINT8)((Length >> 8) & 0xFFu);
    UsbHostCacheCleanRange(SetupPacket, 8u);

    /*
     * 勿给 Control TD 加 Chain：本机 DWC3 根口 hub 无 CH 已绿（ea1）；
     * 加 CH 后真机 e40（Address 后 GetDesc 挂）——以真机为准撤 CH。
     */
    MemoryZero(&Block, sizeof(Block));
    Block.Parameter = *(UINT64 *)(void *)SetupPacket;
    Block.Status = 8u;
    Block.Control = TRANSFER_REQUEST_BLOCK_TYPE(TRANSFER_REQUEST_BLOCK_SETUP) |
                    TRANSFER_REQUEST_BLOCK_IMMEDIATE_DATA |
                    (HasData ? ((RequestType & 0x80u) ? TRANSFER_REQUEST_BLOCK_TRANSFER_TYPE_IN
                                                      : (2u << 16))
                             : 0);
    UsbHostEndpointZeroEnqueue(SlotId, &Block);

    if (HasData) {
        UINTN ByteIndex;
        if ((RequestType & 0x80u) == 0) {
            for (ByteIndex = 0; ByteIndex < Length; ByteIndex++) {
                ControlData[ByteIndex] = ((UINT8 *)Data)[ByteIndex];
            }
        } else {
            MemoryZero(ControlData, Length);
        }
        UsbHostCacheCleanRange(ControlData, Length);
        MemoryZero(&Block, sizeof(Block));
        Block.Parameter = (UINT64)(UINTN)ControlData;
        Block.Status = Length;
        Block.Control = TRANSFER_REQUEST_BLOCK_TYPE(TRANSFER_REQUEST_BLOCK_DATA) |
                        ((RequestType & 0x80u) ? TRANSFER_REQUEST_BLOCK_DIRECTION_IN : 0);
        UsbHostEndpointZeroEnqueue(SlotId, &Block);
    }

    MemoryZero(&Block, sizeof(Block));
    Block.Control = TRANSFER_REQUEST_BLOCK_TYPE(TRANSFER_REQUEST_BLOCK_STATUS) |
                    TRANSFER_REQUEST_BLOCK_INTERRUPT_ON_COMPLETION |
                    ((RequestType & 0x80u) ? 0 : TRANSFER_REQUEST_BLOCK_DIRECTION_IN);
    UsbHostEndpointZeroEnqueue(SlotId, &Block);

    UsbHostRingEndpointZeroDoorbell(SlotId);
    if (UsbHostWaitTransferEvent(&CompletionCode) != 0) {
        sControlLastCompletionCode = 0xFFu;
        return -1;
    }
    sControlLastCompletionCode = CompletionCode;
    if (CompletionCode != COMPLETION_CODE_SUCCESS &&
        CompletionCode != COMPLETION_CODE_SHORT_PACKET) {
        return -1;
    }
    if (HasData && (RequestType & 0x80u) != 0) {
        UINTN ByteIndex;
        UsbHostCacheCleanInvalidateRange(ControlData, Length);
        for (ByteIndex = 0; ByteIndex < Length; ByteIndex++) {
            ((UINT8 *)Data)[ByteIndex] = ControlData[ByteIndex];
        }
    }
    return 0;
}

#else

int UsbHostXhciControlTransfer(UINT32 SlotId, UINT8 RequestType, UINT8 Request,
                               UINT16 Value, UINT16 Index, void *Data, UINT16 Length) {
    (void)SlotId;
    (void)RequestType;
    (void)Request;
    (void)Value;
    (void)Index;
    (void)Data;
    (void)Length;
    return -1;
}

UINT32 UsbHostXhciControlLastCompletionCode(void) {
    return 0;
}

#endif
