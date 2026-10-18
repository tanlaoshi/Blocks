/*
 * UsbHostController.c — 把公共调用转到已注册的主机控制器。
 * 有 xHCI 用 xHCI，否则用 EHCI。
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

int BulkOn(UINT8 Kind, USB_PIPE *Pipe, void *Buf, UINT32 Len, int Out) {
    const USB_HOST_CONTROLLER_OPERATIONS *Operations;

    if (Kind != USB_HOST_CONTROLLER_XHCI && Kind != USB_HOST_CONTROLLER_EHCI) {
        return -1;
    }
    Operations = gOperations[Kind];
    if (Operations == 0 || Operations->Bulk == 0) {
        return -1;
    }
    return Operations->Bulk(Pipe, Buf, Len, Out);
}

static USB_DEVICE gUsbDevice[USB_DEVICE_MAX];

void UsbDeviceNote(UINT8 Address, const UINT8 *Config, UINT16 Total) {
    USB_DEVICE *Dev;
    UINT16 Off;
    int i;

    if (Address == 0 || Config == 0 || Total < 9) {
        return;
    }
    Dev = 0;
    for (i = 0; i < USB_DEVICE_MAX; i++) {
        if (gUsbDevice[i].Address == Address || gUsbDevice[i].Address == 0) {
            Dev = &gUsbDevice[i];
            break;
        }
    }
    if (Dev == 0) {
        return;
    }
    Dev->Address = Address;
    Dev->InterfaceCount = 0;
    Off = 0;
    while (Off + 2 <= Total) {
        UINT8 Len = Config[Off];
        UINT8 Type = Config[Off + 1];
        USB_INTERFACE *Iface;

        if (Len < 2 || (UINT16)(Off + Len) > Total) {
            break;
        }
        if (Type == 4 && Len >= 9) {
            if (Dev->InterfaceCount >= USB_INTERFACE_MAX) {
                break;
            }
            Iface = &Dev->Interface[Dev->InterfaceCount];
            Iface->Class = Config[Off + 5];
            Iface->EndpointCount = 0;
            Dev->InterfaceCount++;
        } else if (Type == 5 && Len >= 7 && Dev->InterfaceCount > 0) {
            Iface = &Dev->Interface[Dev->InterfaceCount - 1];
            if (Iface->EndpointCount < USB_ENDPOINT_MAX) {
                USB_ENDPOINT *Ep = &Iface->Endpoint[Iface->EndpointCount];
                Ep->Address = Config[Off + 2];
                Ep->Attributes = Config[Off + 3];
                Ep->MaxPacket = (UINT16)(Config[Off + 4] | (Config[Off + 5] << 8));
                Iface->EndpointCount++;
            }
        }
        Off = (UINT16)(Off + Len);
    }
}
