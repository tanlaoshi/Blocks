/*
 * PhysicalMemoryPrivate.h — 位图池内部交接（K53 拆文件）
 */
#ifndef PHYSICAL_MEMORY_PRIVATE_H
#define PHYSICAL_MEMORY_PRIVATE_H

#include "BootTypes.h"
#include "PhysicalMemory.h"

#define PHYSICAL_MEMORY_MAX_PAGES 32768u

extern UINT8 gPhysicalMemoryBitmap[(PHYSICAL_MEMORY_MAX_PAGES + 7u) / 8u];
extern UINT64 gPhysicalMemoryBase;
extern UINT32 gPhysicalMemoryPageCount;
extern UINT32 gPhysicalMemoryFreeCount;
extern int gPhysicalMemoryReady;

int PhysicalMemoryBitGet(UINT32 Index);
void PhysicalMemoryBitSet(UINT32 Index);
void PhysicalMemoryBitClear(UINT32 Index);

#endif
