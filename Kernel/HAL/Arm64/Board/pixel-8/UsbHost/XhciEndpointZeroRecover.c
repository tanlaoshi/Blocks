/*
 * UsbHost/XhciEndpointZeroRecover.c — EP0 Stop + SetTrDeq
 *
 * xHCI：Reset EP 仅 Halted 可用；Stop 后是 Stopped → Reset 会 Context State
 * Error（真机 e213=cc0x13）。Sync 只 Stop→SetTrDeq。
 */
#include "UsbHost/Private.h"

#ifndef BOARD_PIXEL8_USB_HOST_BRINGUP
#define BOARD_PIXEL8_USB_HOST_BRINGUP 0
#endif

#if BOARD_PIXEL8_USB_HOST_BRINGUP

int UsbHostEndpointZeroSync(UINT32 SlotId, UINT32 *StopCompletionOut,
                            UINT32 *ResetCompletionOut,
                            UINT32 *DequeueCompletionOut) {
    TransferRequestBlock Command;
    UINT32 CompletionCode;
    UINT64 Dequeue;
    UINT32 EndpointField;
    int Failed = 0;

    if (StopCompletionOut != 0) {
        *StopCompletionOut = 0;
    }
    if (ResetCompletionOut != 0) {
        *ResetCompletionOut = 0; /* 兼容旧调用；不再发 Reset */
    }
    if (DequeueCompletionOut != 0) {
        *DequeueCompletionOut = 0;
    }
    if (SlotId == 0 || SlotId > DEVICE_SLOT_COUNT_MAX) {
        return -1;
    }
    EndpointField = (1u << TRANSFER_REQUEST_BLOCK_ENDPOINT_SHIFT);

    Command.Parameter = 0;
    Command.Status = 0;
    Command.Control = TRANSFER_REQUEST_BLOCK_TYPE(TRANSFER_REQUEST_BLOCK_STOP_ENDPOINT) |
                      TRANSFER_REQUEST_BLOCK_INTERRUPT_ON_COMPLETION |
                      (SlotId << TRANSFER_REQUEST_BLOCK_SLOT_SHIFT) | EndpointField;
    if (UsbHostXhciSubmitCommand(&Command, &CompletionCode, 0) != 0) {
        CompletionCode = 0xFFu;
        Failed = 1;
    }
    if (StopCompletionOut != 0) {
        *StopCompletionOut = CompletionCode;
    }
    if (CompletionCode != COMPLETION_CODE_SUCCESS) {
        Failed = 1;
    }

    UsbHostBindEndpointZeroRing(SlotId);
    Dequeue = UsbHostEndpointZeroRingPhysical(SlotId) | 1ULL;
    Command.Parameter = Dequeue;
    Command.Status = 0;
    Command.Control = TRANSFER_REQUEST_BLOCK_TYPE(TRANSFER_REQUEST_BLOCK_SET_TR_DEQUEUE) |
                      TRANSFER_REQUEST_BLOCK_INTERRUPT_ON_COMPLETION |
                      (SlotId << TRANSFER_REQUEST_BLOCK_SLOT_SHIFT) | EndpointField;
    if (UsbHostXhciSubmitCommand(&Command, &CompletionCode, 0) != 0) {
        CompletionCode = 0xFFu;
        Failed = 1;
    }
    if (DequeueCompletionOut != 0) {
        *DequeueCompletionOut = CompletionCode;
    }
    if (CompletionCode != COMPLETION_CODE_SUCCESS) {
        Failed = 1;
    }
    return Failed != 0 ? -1 : 0;
}

void UsbHostRecoverEndpointZero(UINT32 SlotId) {
    (void)UsbHostEndpointZeroSync(SlotId, 0, 0, 0);
}

#else

int UsbHostEndpointZeroSync(UINT32 SlotId, UINT32 *StopCompletionOut,
                            UINT32 *ResetCompletionOut,
                            UINT32 *DequeueCompletionOut) {
    (void)SlotId;
    (void)StopCompletionOut;
    (void)ResetCompletionOut;
    (void)DequeueCompletionOut;
    return -1;
}

void UsbHostRecoverEndpointZero(UINT32 SlotId) {
    (void)SlotId;
}

#endif
