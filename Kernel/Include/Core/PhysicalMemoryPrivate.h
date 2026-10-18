/*
 * PhysicalMemoryPrivate.h — 段表与框架辅助，仅内核内部。不进公开 API。
 */
#ifndef PHYSICAL_MEMORY_PRIVATE_H
#define PHYSICAL_MEMORY_PRIVATE_H

#include "PhysicalMemory.h"
#include "BootInfo.h"

#define PMM_SEGMENT_SHIFT     30
#define PMM_SEGMENT_SIZE      (1ULL << PMM_SEGMENT_SHIFT)
#define PMM_SEGMENT_COUNT     256
#define PMM_PAGES_PER_SEGMENT (PMM_SEGMENT_SIZE / PAGE_SIZE)

/*
 * 返回的页必须落在恒等映射里，才能当指针用。
 * 三架构统一：恒等窗 4GiB（与 IdentityMap.h / EarlyIdentity 对齐）。
 */
#define PMM_DIRECT_BYTES (4ull << 30)

/* 保留页的引用计数。释放见到它就返回，不改位图、不改计数。 */
#define PMM_REF_RESERVED 0x8000u

typedef struct {
    UINT64  BasePhys;
    UINT32  PageCount;
    UINT32  FreePages;
    UINT8  *Bitmap;
    UINT16 *RefCount;
} PMM_SEGMENT;

void PmmSegmentInit(const BOOT_INFO *Info);
void PmmPhysLockInit(void);
PMM_SEGMENT *PmmSegment(UINT32 Index);
/* 框架：Phys → 段 + 页下标；政策可调用，勿再加锁 */
PMM_SEGMENT *PmmLookup(UINT64 Phys, UINT32 *Idx);

#endif
