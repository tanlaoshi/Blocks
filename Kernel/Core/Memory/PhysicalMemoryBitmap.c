/*
 * PhysicalMemoryBitmap.c — MEMORY_OPS 默认位图政策（K53）
 *
 * 【初学者】
 * - Core/Memory：PhysicalMemoryInitialize 末尾注册本文件的 BitmapOps。
 * - 入口：BitmapAllocatePages / BitmapFreePages（经 MEMORY_OPS vtable）。
 * - 边界：假定 gPhysicalMemory* 已由 PhysicalMemory.c 填好。
 */
#include "PhysicalMemoryPrivate.h"
#include "MemoryOps.h"

/*
 * BitmapInitialize — MEMORY_OPS.Init 钩子
 *
 * 做什么：无状态；位图已在 PhysicalMemory.c 就绪。
 * 谁调用：PhysicalMemoryInitialize 经 MemoryOpsGet()->Init()。
 */
static void BitmapInitialize(void) {
}

/*
 * BitmapAllocatePagesLocked — 连续空闲页首址
 *
 * 做什么：扫描 gPhysicalMemory 位图找 Count 连续 0 位并置 1；不跨池外。
 * 谁调用：PhysicalMemoryAllocatePages（经 MEMORY_OPS vtable）。
 * 前后文：兄弟 — BitmapFreePagesLocked。
 * 返回：物理页 VA；0 表示失败。
 */
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

/*
 * BitmapFreePagesLocked — 归还连续页
 *
 * 做什么：校验对齐与范围后清位图位；重复 free 忽略已清位。
 * 谁调用：PhysicalMemoryFreePages、BitmapReleasePageLocked。
 */
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

/*
 * BitmapRetainPageLocked — 引用计数占位（位图政策无额外 retain）
 *
 * 谁调用：MEMORY_OPS vtable（本刀恒成功）。
 * 返回：0 成功；-1 未就绪。
 */
static int BitmapRetainPageLocked(void *Page) {
    if (!gPhysicalMemoryReady || Page == 0) {
        return -1;
    }
    return 0;
}

/* BitmapReleasePageLocked — 单页释放；委托 BitmapFreePagesLocked(Page,1)。 */
static void BitmapReleasePageLocked(void *Page) {
    BitmapFreePagesLocked(Page, 1);
}

/*
 * MemoryBitmapOps — 默认 PMM 政策 vtable
 *
 * 做什么：返回静态 MEMORY_OPS（位图 alloc/free）。
 * 谁调用：PhysicalMemoryInitialize → MemoryOpsRegister。
 * 返回：只读 Ops 指针。
 */
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
