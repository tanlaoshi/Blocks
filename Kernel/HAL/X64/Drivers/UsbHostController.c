/*
 * UsbHostController.c — 把公共调用转到已注册的主机控制器。
 * 当前只认 xHCI。EHCI 作为后备是下一刀。
 */
#include "UsbHostController.h"

static const USB_HOST_CONTROLLER_OPERATIONS *gOperations[3];

static const USB_HOST_CONTROLLER_OPERATIONS *Current(void) {
    if (gOperations[USB_HOST_CONTROLLER_XHCI] != 0) {
        return gOperations[USB_HOST_CONTROLLER_XHCI];
    }
    return gOperations[USB_HOST_CONTROLLER_EHCI];
}

int Register(UINT8 Kind, const USB_HOST_CONTROLLER_OPERATIONS *Operations) {
    if ((Kind != USB_HOST_CONTROLLER_XHCI && Kind != USB_HOST_CONTROLLER_EHCI) || Operations == 0) {
        return -1;
    }
    gOperations[Kind] = Operations;
    return 0;
}

void Unregister(UINT8 Kind) {
    if (Kind == USB_HOST_CONTROLLER_XHCI || Kind == USB_HOST_CONTROLLER_EHCI) {
        gOperations[Kind] = 0;
    }
}

int ClaimInterface(USB_PIPE *Pipe, UINT8 Class, UINT8 Subclass, UINT8 Protocol) {
    (void)Pipe;
    (void)Class;
    (void)Subclass;
    (void)Protocol;
    return -1;
}

int Control(USB_PIPE *Pipe, USB_SETUP *Setup, void *Data, UINT32 Len) {
    const USB_HOST_CONTROLLER_OPERATIONS *Operations = Current();

    (void)Len;
    if (Operations == 0 || Operations->Control == 0) {
        return -1;
    }
    return Operations->Control(Pipe, Setup, Data, Len);
}

int InterruptQueue(USB_PIPE *Pipe, void *Buf, UINT32 Len, void (*Done)(void *Context)) {
    const USB_HOST_CONTROLLER_OPERATIONS *Operations = Current();

    if (Operations == 0 || Operations->InterruptQueue == 0) {
        return -1;
    }
    return Operations->InterruptQueue(Pipe, Buf, Len, Done);
}

int Bulk(USB_PIPE *Pipe, void *Buf, UINT32 Len, int Out) {
    const USB_HOST_CONTROLLER_OPERATIONS *Operations = Current();

    if (Operations == 0 || Operations->Bulk == 0) {
        return -1;
    }
    return Operations->Bulk(Pipe, Buf, Len, Out);
}
