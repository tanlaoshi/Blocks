/*
 * HalBlockStub.c — 非 X64：无块设备
 *
 * 【初学者】
 * - Hal/Common 占位；Core Volume 调用 HalBlock* 得 -1/未就绪。
 * - 真实现：Hal/X64/HalVirtioBlk.c。
 */
#include "HalBlock.h"

int HalBlockInitialize(void) {
    return -1;
}

int HalBlockReady(void) {
    return 0;
}

int HalBlockDriveCount(void) {
    return 0;
}

int HalBlockSelect(int Drive) {
    (void)Drive;
    return -1;
}

int HalBlockCurrent(void) {
    return 0;
}

int HalBlockRead(UINT64 Lba, void *Buf, UINT32 Count) {
    (void)Lba;
    (void)Buf;
    (void)Count;
    return -1;
}

int HalBlockWrite(UINT64 Lba, const void *Buf, UINT32 Count) {
    (void)Lba;
    (void)Buf;
    (void)Count;
    return -1;
}
