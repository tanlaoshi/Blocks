/*
 * HalXhciStub.c — 非 X64：xHCI 门面空实现
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
