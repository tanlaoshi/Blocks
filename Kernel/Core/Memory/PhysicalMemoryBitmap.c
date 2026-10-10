/*
 * PhysicalMemoryBitmap.c — MEMORY_OPS 默认位图政策（K53）
 */
#include "PhysicalMemoryPrivate.h"
#include "MemoryOps.h"

static void BitmapInitialize(void) {
}

static void *BitmapAllocatePagesLocked(UINT32 Count) {
    UINT32 Start;
    UINT32 Index;
    UINT32 Run;

    if (!gPhysicalMemoryReady || Count == 0 || Count > gPhysicalMemoryPageCount ||
        Count > gPhysicalMemoryFreeCount) {
        return 0;
    }
    Run = 0;
    for (Start = 0; Start < gPhysicalMemoryPageCount; Start++) {
        if (PhysicalMemoryBitGet(Start)) {
            Run = 0;
            continue;
        }
        Run++;
        if (Run < Count) {
            continue;
        }
        {
            UINT32 First = Start + 1u - Count;
            for (Index = 0; Index < Count; Index++) {
                PhysicalMemoryBitSet(First + Index);
            }
            gPhysicalMemoryFreeCount -= Count;
            return (void *)(UINTN)(gPhysicalMemoryBase +
                                   ((UINT64)First << PAGE_SHIFT));
        }
    }
    return 0;
}

static void BitmapFreePagesLocked(void *Page, UINT32 Count) {
    UINT64 Physical;
    UINT32 First;
    UINT32 Index;

    if (!gPhysicalMemoryReady || Page == 0 || Count == 0) {
        return;
    }
    Physical = (UINT64)(UINTN)Page;
    if (Physical < gPhysicalMemoryBase ||
        (Physical - gPhysicalMemoryBase) % PAGE_SIZE != 0) {
        return;
    }
    First = (UINT32)((Physical - gPhysicalMemoryBase) >> PAGE_SHIFT);
    if (First >= gPhysicalMemoryPageCount ||
        Count > gPhysicalMemoryPageCount - First) {
        return;
    }
    for (Index = 0; Index < Count; Index++) {
        if (PhysicalMemoryBitGet(First + Index)) {
            PhysicalMemoryBitClear(First + Index);
            gPhysicalMemoryFreeCount++;
        }
    }
}

static int BitmapRetainPageLocked(void *Page) {
    if (!gPhysicalMemoryReady || Page == 0) {
        return -1;
    }
    return 0;
}

static void BitmapReleasePageLocked(void *Page) {
    BitmapFreePagesLocked(Page, 1);
}

const MEMORY_OPS *MemoryBitmapOps(void) {
    static const MEMORY_OPS Ops = {
        BitmapInitialize,
        BitmapAllocatePagesLocked,
        BitmapFreePagesLocked,
        BitmapRetainPageLocked,
        BitmapReleasePageLocked,
    };
    return &Ops;
}
