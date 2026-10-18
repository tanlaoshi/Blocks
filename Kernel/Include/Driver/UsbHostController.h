/*
 * UsbHostController.h — USB 主机控制器公共口。
 * 类驱动只走这里，不包含 xHCI / EHCI 私头。
 */
#ifndef USB_HOST_CONTROLLER_H
#define USB_HOST_CONTROLLER_H

#include "BootTypes.h"

#define USB_HOST_CONTROLLER_NONE 0
#define USB_HOST_CONTROLLER_XHCI 1
#define USB_HOST_CONTROLLER_EHCI 2
#define USB_HOST_CONTROLLER_UHCI 3

typedef struct {
    UINT8 bmRequestType;
    UINT8 bRequest;
    UINT16 wValue;
    UINT16 wIndex;
    UINT16 wLength;
} __attribute__((packed)) USB_SETUP;

typedef struct USB_PIPE {
    void *HostControllerPrivate;
    void *DoneContext;
} USB_PIPE;

typedef struct USB_HOST_CONTROLLER_OPERATIONS {
    int (*Init)(UINT64 BaseAddress);
    int (*Control)(USB_PIPE *Pipe, USB_SETUP *Setup, void *Data, UINT32 Len);
    int (*InterruptQueue)(USB_PIPE *Pipe, void *Buf, UINT32 Len, void (*Done)(void *Context));
    int (*Bulk)(USB_PIPE *Pipe, void *Buf, UINT32 Len, int Out);
} USB_HOST_CONTROLLER_OPERATIONS;

int Attach(UINT8 Kind, const USB_HOST_CONTROLLER_OPERATIONS *Operations);
void Detach(UINT8 Kind);
int ClaimInterface(USB_PIPE *Pipe, UINT8 Class, UINT8 Subclass, UINT8 Protocol);
const USB_HOST_CONTROLLER_OPERATIONS *HostOperations(void);
/* 管道由 ClaimInterface 填好。Class 3、Protocol 1 是键盘，其余 HID 是鼠标。 */
int InterruptByClass(UINT8 Class, UINT8 Subclass, UINT8 Protocol);
int BulkOn(UINT8 Kind, USB_PIPE *Pipe, void *Buf, UINT32 Len, int Out);

extern USB_HOST_CONTROLLER_OPERATIONS gXhciOperations;
void PublishControllerInit(void);

/* 枚举记录。认领按接口类在这张表里找。 */
#define USB_DEVICE_MAX    8
#define USB_INTERFACE_MAX 4
#define USB_ENDPOINT_MAX  6

typedef struct {
    UINT8  Address;
    UINT8  Attributes;
    UINT16 MaxPacket;
} USB_ENDPOINT;

typedef struct {
    UINT8 Class;
    UINT8 Subclass;
    UINT8 Protocol;
    UINT8 EndpointCount;
    USB_ENDPOINT Endpoint[USB_ENDPOINT_MAX];
} USB_INTERFACE;

typedef struct {
    UINT8 Address;
    UINT8 InterfaceCount;
    USB_INTERFACE Interface[USB_INTERFACE_MAX];
} USB_DEVICE;

void UsbDeviceNote(UINT8 Address, const UINT8 *Config, UINT16 Total);

#endif /* USB_HOST_CONTROLLER_H */
