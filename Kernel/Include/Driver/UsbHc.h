/*
 * UsbHc.h — USB 主机控制器公共口。
 * 类驱动只走这里，不包含 xHCI / EHCI 私头。
 * 本刀只有声明和空实现，没有调用方。
 */
#ifndef USB_HC_H
#define USB_HC_H

#include "BootTypes.h"

#define USB_HC_NONE 0
#define USB_HC_XHCI 1
#define USB_HC_EHCI 2

typedef struct {
    UINT8 bmRequestType;
    UINT8 bRequest;
    UINT16 wValue;
    UINT16 wIndex;
    UINT16 wLength;
} __attribute__((packed)) USB_SETUP;

typedef struct USB_PIPE {
    void *HcPriv;
    void *DoneCtx;
} USB_PIPE;

typedef struct USB_HC_OPS {
    int (*Control)(USB_PIPE *Pipe, USB_SETUP *Setup, void *Data, UINT32 Len);
    int (*IntrQueue)(USB_PIPE *Pipe, void *Buf, UINT32 Len, void (*Done)(void *Ctx));
    int (*Bulk)(USB_PIPE *Pipe, void *Buf, UINT32 Len, int Out);
} USB_HC_OPS;

int UsbHcRegister(UINT8 Kind, const USB_HC_OPS *Ops);
void UsbHcUnregister(UINT8 Kind);
int UsbHcClaimInterface(USB_PIPE *Pipe, UINT8 Class, UINT8 Subclass, UINT8 Protocol);
int UsbHcControl(USB_PIPE *Pipe, USB_SETUP *Setup, void *Data, UINT32 Len);
int UsbHcIntrQueue(USB_PIPE *Pipe, void *Buf, UINT32 Len, void (*Done)(void *Ctx));
int UsbHcBulk(USB_PIPE *Pipe, void *Buf, UINT32 Len, int Out);

#endif /* USB_HC_H */
