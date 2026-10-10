/*
 * HalBlock.h — 块设备门面（K16 读；K44 多盘 Select）
 *
 * X64：最多 2 个 legacy virtio-blk；其它 Arch：stub。
 */
#ifndef HAL_BLOCK_H
#define HAL_BLOCK_H

#include "BootTypes.h"

#define HAL_BLOCK_MAX_DRIVES 2

int HalBlockInitialize(void);
int HalBlockReady(void);
int HalBlockDriveCount(void);
/* 选当前盘 0..Count-1；成功 0 */
int HalBlockSelect(int Drive);
int HalBlockCurrent(void);
/* 读/写当前盘 Count 个扇区；成功 0 */
int HalBlockRead(UINT64 Lba, void *Buf, UINT32 Count);
int HalBlockWrite(UINT64 Lba, const void *Buf, UINT32 Count);

#endif
