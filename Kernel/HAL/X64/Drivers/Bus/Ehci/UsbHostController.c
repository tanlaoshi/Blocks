/*
 * UsbHostController.c — 把 EHCI 现有控制、中断、批量封进公共口。
 * 键鼠和 U 盘仍直接调原来的函数。
 */
#include "UsbHostController.h"
#include "Ehci/Private.h"

static EHCI_CTRL *Running(void) {
    int i;

    if (!gEhciReady) {
        return 0;
    }
    for (i = 0; i < gEhciCount; i++) {
        if (gEhci[i].Up && gEhci[i].Sched) {
            return &gEhci[i];
        }
    }
    return 0;
}

static int SubmitControl(USB_PIPE *Pipe, USB_SETUP *Setup, void *Data, UINT32 Len) {
    EHCI_CTRL *Controller = Running();
    UINT8 Address = 0;
    UINT8 EndpointMax = 8;

    (void)Pipe;
    (void)Len;
    if (Controller == 0 || Setup == 0) {
        return -1;
    }
    if (Controller->HidOk) {
        Address = Controller->DevAddr;
    } else if (Controller->MscOk) {
        Address = Controller->MscAddr;
        EndpointMax = Controller->MscEpMax0 != 0 ? Controller->MscEpMax0 : 64;
    }
    return ControlTransfer(Controller, Address, EndpointMax,
                            (const USB_SETUP_PACKET *)Setup, Data);
}

static int QueueInterrupt(USB_PIPE *Pipe, void *Buf, UINT32 Len, void (*Done)(void *Context)) {
    int i;

    (void)Pipe;
    (void)Buf;
    (void)Len;
    (void)Done;
    if (!gEhciReady) {
        return -1;
    }
    for (i = 0; i < gEhciCount; i++) {
        if (gEhci[i].HidOk) {
            HidArmIntr(&gEhci[i]);
            return 0;
        }
    }
    return -1;
}

static int BulkTransfer(USB_PIPE *Pipe, void *Buf, UINT32 Len, int Out) {
    EHCI_CTRL *Controller = gEhciMscCtrl;

    (void)Pipe;
    if (Controller == 0 || !Controller->MscOk) {
        return -1;
    }
    Controller->XferSpeed = Controller->MscSpeed;
    Controller->XferHubAddr = Controller->MscHubAddr;
    Controller->XferHubPort = Controller->MscHubPort;
    if (Out) {
        return BulkXfer(Controller, Controller->MscAddr, Controller->MscEpOut,
                             Controller->MscMpsOut, Controller->MscSpeed,
                             Controller->MscHubAddr, Controller->MscHubPort, 0,
                             Buf, Len, &Controller->MscDtOut);
    }
    return BulkXfer(Controller, Controller->MscAddr, Controller->MscEpIn,
                         Controller->MscMpsIn, Controller->MscSpeed,
                         Controller->MscHubAddr, Controller->MscHubPort, 1,
                         Buf, Len, &Controller->MscDtIn);
}

static const USB_HOST_CONTROLLER_OPERATIONS gOperations = {
    .Control = SubmitControl,
    .InterruptQueue = QueueInterrupt,
    .Bulk = BulkTransfer,
};

void Offer(void) {
    Attach(USB_HOST_CONTROLLER_EHCI, &gOperations);
}
