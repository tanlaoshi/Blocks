/*
 * HalBlock.h — 块设备门面（K16 最小）
 *
 * 读 512 字节扇区。X64：virtio-blk；其它 Arch：stub。
 */
#ifndef HAL_BLOCK_H
#define HAL_BLOCK_H

#include "BootTypes.h"

int HalBlockInit(void);
int HalBlockReady(void);
/* 读 Count 个扇区到 Buf（须 512*Count 字节）；成功 0 */
int HalBlockRead(UINT64 Lba, void *Buf, UINT32 Count);

#endif
