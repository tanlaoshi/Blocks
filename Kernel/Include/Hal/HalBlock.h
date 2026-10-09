/*
 * HalBlock.h — 块设备门面（K16 读；K24 写）
 *
 * 扇区 512 字节。X64：virtio-blk；其它 Arch：stub。
 */
#ifndef HAL_BLOCK_H
#define HAL_BLOCK_H

#include "BootTypes.h"

int HalBlockInit(void);
int HalBlockReady(void);
/* 读/写 Count 个扇区；Buf 须 512*Count；成功 0 */
int HalBlockRead(UINT64 Lba, void *Buf, UINT32 Count);
int HalBlockWrite(UINT64 Lba, const void *Buf, UINT32 Count);

#endif
