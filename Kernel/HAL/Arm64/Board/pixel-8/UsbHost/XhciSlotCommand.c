/*
 * UsbHost/XhciSlotCommand.c — DisableSlot / ConfigureEndpoint
 */
#include "UsbHost/Private.h"

#ifndef BOARD_PIXEL8_USB_HOST_BRINGUP
#define BOARD_PIXEL8_USB_HOST_BRINGUP 0
#endif

#if BOARD_PIXEL8_USB_HOST_BRINGUP

static UINT32 sConfigureEndpointCompletionCode;

UINT32 UsbHostXhciConfigureEndpointCompletionCode(void) {
    return sConfigureEndpointCompletionCode;
}

int UsbHostXhciDisableSlot(UINT32 SlotId) {
    TransferRequestBlock Command;
    UINT32 CompletionCode = 0;

    Command.Parameter = 0;
    Command.Status = 0;
    Command.Control = TRANSFER_REQUEST_BLOCK_TYPE(TRANSFER_REQUEST_BLOCK_DISABLE_SLOT) |
                      TRANSFER_REQUEST_BLOCK_INTERRUPT_ON_COMPLETION |
                      (SlotId << TRANSFER_REQUEST_BLOCK_SLOT_SHIFT);
    if (UsbHostXhciSubmitCommand(&Command, &CompletionCode, 0) != 0 ||
        CompletionCode != COMPLETION_CODE_SUCCESS) {
        return -1;
    }
    return 0;
}

int UsbHostXhciConfigureEndpoint(UINT32 SlotId) {
    TransferRequestBlock Command;
    UINT32 CompletionCode = 0;

    sConfigureEndpointCompletionCode = 0;
    Command.Parameter = (UINT64)(UINTN)UsbHostInputContextBuffer();
    Command.Status = 0;
    Command.Control =
        TRANSFER_REQUEST_BLOCK_TYPE(TRANSFER_REQUEST_BLOCK_CONFIGURE_ENDPOINT) |
        TRANSFER_REQUEST_BLOCK_INTERRUPT_ON_COMPLETION |
        (SlotId << TRANSFER_REQUEST_BLOCK_SLOT_SHIFT);
    if (UsbHostXhciSubmitCommand(&Command, &CompletionCode, 0) != 0) {
        sConfigureEndpointCompletionCode = 0xFFu;
        return -1;
    }
    sConfigureEndpointCompletionCode = CompletionCode;
    if (CompletionCode != COMPLETION_CODE_SUCCESS) {
        return -1;
    }
    return 0;
}

#else

UINT32 UsbHostXhciConfigureEndpointCompletionCode(void) {
    return 0;
}

int UsbHostXhciDisableSlot(UINT32 SlotId) {
    (void)SlotId;
    return -1;
}

int UsbHostXhciConfigureEndpoint(UINT32 SlotId) {
    (void)SlotId;
    return -1;
}

#endif
