/*
 * Block.h — 块设备读写抽象（与文件系统解耦）
 *
 * 后端经 BlockRegisterBackend / DriverBlockAttach（PR-D2）注册；
 * x86 ATA / AHCI（PR-H1）/ NVMe（PR-H5）、virt Arm/RiscV virtio-blk 经 Driver Block 类挂上。
 */
#ifndef BLOCK_H
#define BLOCK_H

#include "BootTypes.h"

#define BLOCK_SECTOR_SIZE 512
#define BLOCK_MAX_DRIVES  4  /* PR-FS-inst-1：第三盘作安装目标（AHCI） */

typedef struct {
    int (*Probe)(UINT32 Drive);
    int (*ReadSectors)(UINT32 Drive, UINT32 Lba, UINT32 Count, void *Buffer);
    int (*WriteSectors)(UINT32 Drive, UINT32 Lba, UINT32 Count, const void *Buffer);
    /* 可选：把写缓冲刷到介质；NULL → BlockFlush 成功空操作（PR-F2） */
    int (*Flush)(UINT32 Drive);
} BLOCK_BACKEND;

void BlockRegisterBackend(const BLOCK_BACKEND *Backend);
/*
 * Mux 等外层包装：不进多后端表；传 NULL 卸外层，active 回到 Primary/Fan。
 */
void BlockRegisterOuterBackend(const BLOCK_BACKEND *Backend);
/* 当前 active（可能是 Mux）；装 MSC 前请用 PeekPrimary */
const BLOCK_BACKEND *BlockPeekBackend(void);
/* 内置盘侧：单后端或 Fan（AHCI+NVMe 并存）；不含 Mux */
const BLOCK_BACKEND *BlockPeekPrimaryBackend(void);

int BlockSelect(UINT32 Drive);
UINT32 BlockSelectedDrive(void);
UINT32 BlockCurrentDrive(void);
int BlockInitialize(void);
/* 是否已挂后端（PR-D2：Driver Bind 之后为真） */
int BlockBackendReady(void);
/* PR-FS-inst-1：槽位是否 Probe 成功 */
int BlockDriveReady(UINT32 Drive);
int BlockReadSectors(UINT32 Lba, UINT32 Count, void *Buffer);
int BlockWriteSectors(UINT32 Lba, UINT32 Count, const void *Buffer);
/* PR-F2：落盘；无 Flush 回调时返回 1（成功） */
int BlockFlush(void);

#endif
