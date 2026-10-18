/*
 * UsbHostController.c — 把公共调用转到已注册的主机控制器。
 * 优先级：有 xHCI 用 xHCI，否则 EHCI，再否则 UHCI。
 */
#include "UsbHostController.h"

static const USB_HOST_CONTROLLER_OPERATIONS *gOperations[4];
static USB_DEVICE gUsbDevice[USB_DEVICE_MAX];
static USB_INTERFACE gKeyboardFallback = { .Class = 3, .Protocol = 1 };
static USB_INTERFACE gMouseFallback = { .Class = 3, .Protocol = 2 };
static USB_PIPE gKeyboardPipe;
static USB_PIPE gMousePipe;

static int KnownKind(UINT8 Kind) {
    return Kind == USB_HOST_CONTROLLER_XHCI ||
           Kind == USB_HOST_CONTROLLER_EHCI ||
           Kind == USB_HOST_CONTROLLER_UHCI;
}

static const USB_HOST_CONTROLLER_OPERATIONS *Current(void) {
    if (gOperations[USB_HOST_CONTROLLER_XHCI] != 0) {
        return gOperations[USB_HOST_CONTROLLER_XHCI];
    }
    if (gOperations[USB_HOST_CONTROLLER_EHCI] != 0) {
        return gOperations[USB_HOST_CONTROLLER_EHCI];
    }
    return gOperations[USB_HOST_CONTROLLER_UHCI];
}

static int Register(UINT8 Kind, const USB_HOST_CONTROLLER_OPERATIONS *Operations) {
    if (!KnownKind(Kind) || Operations == 0) {
        return -1;
    }
    gOperations[Kind] = Operations;
    return 0;
}

static void Unregister(UINT8 Kind) {
    if (KnownKind(Kind)) {
        gOperations[Kind] = 0;
    }
}

int Attach(UINT8 Kind, const USB_HOST_CONTROLLER_OPERATIONS *Operations) {
    return Register(Kind, Operations);
}

void Detach(UINT8 Kind) {
    Unregister(Kind);
}

const USB_HOST_CONTROLLER_OPERATIONS *HostOperations(void) {
    return Current();
}

int ClaimInterface(USB_PIPE *Pipe, UINT8 Class, UINT8 Subclass, UINT8 Protocol) {
    int DevIndex;
    int IfaceIndex;

    if (Pipe == 0) {
        return -1;
    }
    for (DevIndex = 0; DevIndex < USB_DEVICE_MAX; DevIndex++) {
        USB_DEVICE *Dev = &gUsbDevice[DevIndex];
        if (Dev->Address == 0) {
            continue;
        }
        for (IfaceIndex = 0; IfaceIndex < Dev->InterfaceCount; IfaceIndex++) {
            USB_INTERFACE *Iface = &Dev->Interface[IfaceIndex];
            if (Iface->Class != Class) {
                continue;
            }
            /* 3,0,0：鼠标或 tablet，跳过 boot 键盘 */
            if (Class == 3 && Subclass == 0 && Protocol == 0) {
                if (Iface->Protocol == 1) {
                    continue;
                }
            } else {
                if (Subclass != 0xFF && Iface->Subclass != Subclass) {
                    continue;
                }
                if (Protocol != 0xFF && Iface->Protocol != Protocol) {
                    continue;
                }
            }
            Pipe->HostControllerPrivate = Iface;
            Pipe->DoneContext = 0;
            return 0;
        }
    }
    return -1;
}

static int InterruptQueue(USB_PIPE *Pipe, void *Buf, UINT32 Len, void (*Done)(void *Context)) {
    const USB_HOST_CONTROLLER_OPERATIONS *Operations = Current();

    if (Operations == 0 || Operations->InterruptQueue == 0) {
        return -1;
    }
    return Operations->InterruptQueue(Pipe, Buf, Len, Done);
}

int InterruptByClass(UINT8 Class, UINT8 Subclass, UINT8 Protocol) {
    USB_PIPE *Pipe = (Protocol == 1) ? &gKeyboardPipe : &gMousePipe;

    if (ClaimInterface(Pipe, Class, Subclass, Protocol) != 0) {
        Pipe->HostControllerPrivate = (Protocol == 1) ? &gKeyboardFallback : &gMouseFallback;
        Pipe->DoneContext = 0;
    }
    return InterruptQueue(Pipe, 0, 0, 0);
}

int BulkOn(UINT8 Kind, USB_PIPE *Pipe, void *Buf, UINT32 Len, int Out) {
    const USB_HOST_CONTROLLER_OPERATIONS *Operations;

    if (!KnownKind(Kind)) {
        return -1;
    }
    Operations = gOperations[Kind];
    if (Operations == 0 || Operations->Bulk == 0) {
        return -1;
    }
    return Operations->Bulk(Pipe, Buf, Len, Out);
}

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
            Iface->Subclass = Config[Off + 6];
            Iface->Protocol = Config[Off + 7];
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
