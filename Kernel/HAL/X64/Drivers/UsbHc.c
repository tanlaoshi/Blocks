/*
 * UsbHc.c — 公共口空实现。尚无调用方，返回 -1。
 */
#include "UsbHc.h"

int UsbHcRegister(UINT8 Kind, const USB_HC_OPS *Ops) {
    (void)Kind;
    (void)Ops;
    return -1;
}

void UsbHcUnregister(UINT8 Kind) {
    (void)Kind;
}

int UsbHcClaimInterface(USB_PIPE *Pipe, UINT8 Class, UINT8 Subclass, UINT8 Protocol) {
    (void)Pipe;
    (void)Class;
    (void)Subclass;
    (void)Protocol;
    return -1;
}

int UsbHcControl(USB_PIPE *Pipe, USB_SETUP *Setup, void *Data, UINT32 Len) {
    (void)Pipe;
    (void)Setup;
    (void)Data;
    (void)Len;
    return -1;
}

int UsbHcIntrQueue(USB_PIPE *Pipe, void *Buf, UINT32 Len, void (*Done)(void *Ctx)) {
    (void)Pipe;
    (void)Buf;
    (void)Len;
    (void)Done;
    return -1;
}

int UsbHcBulk(USB_PIPE *Pipe, void *Buf, UINT32 Len, int Out) {
    (void)Pipe;
    (void)Buf;
    (void)Len;
    (void)Out;
    return -1;
}
