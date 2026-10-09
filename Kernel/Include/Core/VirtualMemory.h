/*
 * VirtualMemory.h — 虚拟内存门面
 *
 * 【初学者】
 * X64 上 EarlyIdentity 已打开 4GiB 恒等分页。
 * K5：认领 PG / FB 在窗内。
 * K13：MapMmio 把窗外高址 BAR 映进当前页表（虚址=物理址）。
 */
#ifndef VIRTUAL_MEMORY_H
#define VIRTUAL_MEMORY_H

#include "BootTypes.h"

int VirtualMemoryInitialize(void);
void VirtualMemoryEnable(void);
/* 把 [Phys, Phys+Size) 映成恒等；已在 4GiB 窗内则成功空操作 */
int VirtualMemoryMapIdentity(UINT64 Phys, UINT64 Size);
/*
 * 把 MMIO [Phys, Phys+Size) 映成可访问虚址（X64：虚址=物理，2MiB 大页）。
 * OutVirt 可为 NULL。成功 0。
 */
int VirtualMemoryMapMmio(UINT64 Phys, UINT64 Size, UINT64 *OutVirt);

#endif
