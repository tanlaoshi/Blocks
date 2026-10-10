/*
 * HalXhciStub.c — 非 X64：无 xHCI MMIO
 *
 * 【初学者】
 * - Hal 占位；Usb.c 见 HalXhciReady==0 则跳过 USB 栈。
 * - 真实现：Hal/X64/HalXhci.c + Drivers/UsbHid/。
 */
#include "HalXhci.h"

int HalXhciAttach(UINT64 Virt) {
    (void)Virt;
    return -1;
}

int HalXhciReady(void) {
    return 0;
}

int HalXhciReset(void) {
    return -1;
}

UINT32 HalXhciPortCount(void) {
    return 0;
}

int HalXhciPortCcs(UINT32 Port) {
    (void)Port;
    return -1;
}
