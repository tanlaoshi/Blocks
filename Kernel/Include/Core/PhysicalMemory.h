/*
 * PhysicalMemory.h — 物理页分配器（PMM）对外契约
 *
 * 【初学者】
 * 管的是 4KiB 物理页，不是 malloc 小对象。
 * Initialize 读 BOOT_INFO 的 Regions；Allocate/Free 交还页。
 *
 * 【积木】本刀是默认位图实现；政策面以后可抽 MEMORY_OPS（现网已有范本）。
 */
#ifndef PHYSICAL_MEMORY_H
#define PHYSICAL_MEMORY_H

#include "BootTypes.h"

#define PAGE_SHIFT 12
#define PAGE_SIZE  4096u

int PhysicalMemoryInitialize(void);

void *PhysicalMemoryAllocatePage(void);
void *PhysicalMemoryAllocatePages(UINT32 Count);
void PhysicalMemoryFreePage(void *Page);
void PhysicalMemoryFreePages(void *Page, UINT32 Count);

/* COW 占位：本刀 Retain 成功、Release 即 Free */
int  PhysicalMemoryRetainPage(void *Page);
void PhysicalMemoryReleasePage(void *Page);

UINT64 PhysicalMemoryTotalPages(void);
UINT64 PhysicalMemoryFreePageCount(void);

#endif
