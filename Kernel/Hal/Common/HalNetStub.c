/*
 * HalNetStub.c — 非 X64：无 virtio-net
 *
 * 【初学者】
 * - Hal 占位；NetworkInitialize 探测 PCI 但 HalNetReady==0。
 * - 真实现：Hal/X64/HalVirtioNet.c。
 */
#include "HalNet.h"

int HalNetInitialize(void) {
    return -1;
}

int HalNetReady(void) {
    return 0;
}

void HalNetGetMac(UINT8 Mac[6]) {
    UINTN i;
    for (i = 0; i < 6; i++) {
        Mac[i] = 0;
    }
}

int HalNetTransmit(const void *Frame, UINT32 Len) {
    (void)Frame;
    (void)Len;
    return -1;
}

int HalNetReceive(void *Buf, UINT32 Cap) {
    (void)Buf;
    (void)Cap;
    return -1;
}
