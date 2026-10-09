/*
 * HalBlockStub.c — 非 X64：无块设备
 */
#include "HalBlock.h"

int HalBlockInit(void) {
    return -1;
}

int HalBlockReady(void) {
    return 0;
}

int HalBlockRead(UINT64 Lba, void *Buf, UINT32 Count) {
    (void)Lba;
    (void)Buf;
    (void)Count;
    return -1;
}
