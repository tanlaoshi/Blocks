#include "HalNet.h"

int HalNetInit(void) {
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
