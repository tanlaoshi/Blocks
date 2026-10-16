/*
 * UsbHostController.c — 把现有控制 / 键盘中断 / MSC 批量封进主机控制器公共口。
 * HID 与 MSC 仍走原来的函数，本文件没有新调用方。
 */
#include "UsbHostController.h"
#include "XHCI/Internal.h"

static int HostControllerControl(USB_PIPE *Pipe, USB_SETUP *Setup, void *Data, UINT32 Len) {
    (void)Pipe;
    (void)Len;
    if (Setup == 0 || !gXhciStarted || gSlotId == 0) {
        return -1;
    }
    gXferSlot = gSlotId;
    return ControlXfer((USB_SETUP_PACKET *)Setup, Data);
}

static int HostControllerInterruptQueue(USB_PIPE *Pipe, void *Buf, UINT32 Len,
                                            void (*Done)(void *Context)) {
    (void)Buf;
    (void)Len;
    (void)Done;
    if (!gXhciStarted) {
        return -1;
    }
    if (Pipe != 0) {
        if (gMouseSlotId == 0 || gMouseIntrDci == 0) {
            return -1;
        }
        QueueMouseIntr();
        return 0;
    }
    if (gSlotId == 0 || gIntrDci == 0) {
        return -1;
    }
    QueueIntr();
    return 0;
}

static int HostControllerBulk(USB_PIPE *Pipe, void *Buf, UINT32 Len, int Out) {
    (void)Pipe;
    if (!gXhciStarted) {
        return -1;
    }
    return XhciBulkXfer(Out ? 0 : 1, Buf, Len);
}

static const USB_HOST_CONTROLLER_OPERATIONS gXhciHostControllerOperations = {
    .Control = HostControllerControl,
    .InterruptQueue = HostControllerInterruptQueue,
    .Bulk = HostControllerBulk,
};

void UsbHostControllerOffer(void) {
    Register(USB_HOST_CONTROLLER_XHCI, &gXhciHostControllerOperations);
}

void UsbHostControllerRetract(void) {
    Unregister(USB_HOST_CONTROLLER_XHCI);
}
