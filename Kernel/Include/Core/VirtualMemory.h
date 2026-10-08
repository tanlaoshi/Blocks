/*
 * VirtualMemory.h — 虚拟内存门面（K5 最小子集）
 *
 * 【初学者】
 * X64 上 EarlyIdentity 已在 KernelMain 打开 4GiB 恒等分页。
 * 本模块认领该状态：确认 PG、确认 FB 落在窗内；正式换表/用户空间另刀。
 */
#ifndef VIRTUAL_MEMORY_H
#define VIRTUAL_MEMORY_H

#include "BootTypes.h"

int VirtualMemoryInitialize(void);
void VirtualMemoryEnable(void);
/* 把 [Phys, Phys+Size) 映成恒等；已在 4GiB 窗内则成功空操作 */
int VirtualMemoryMapIdentity(UINT64 Phys, UINT64 Size);

#endif
