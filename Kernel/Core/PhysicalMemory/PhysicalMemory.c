/*
 * PhysicalMemory.c — 物理页分配器入口（开机模块表名 "Memory"）。
 *
 * 位图/课设策略同夹；曾单独有 Core/Memory/ 只包一层 Init，已并回。
 */
#include "KernelModulesPrivate.h"
#include "PhysicalMemory.h"
#include "PhysicalMemoryPrivate.h"
#include "MemoryOps.h"
#include "BootInfo.h"
#include "Debug.h"

void PmmPhysLockInit(void);

int InitializePhysicalMemory(void)
{
    return PhysicalMemoryInitialize();
}

int PhysicalMemoryInitialize(void)
{
    const BOOT_INFO *Info = BootInfoGet();
    const MEMORY_OPS *Ops;

    if (Info == 0 || Info->RegionCount == 0) {
        return -1;
    }
    PmmPhysLockInit();
    PmmSegmentInit(Info);
#ifdef TOY_MEM_BESTFIT
    MemoryOpsRegister(MemoryBestFitOps());
#else
    MemoryOpsRegister(MemoryBitmapOps());
#endif
    Ops = MemoryOpsGet();
    if (Ops && Ops->Init) {
        Ops->Init();
    }
    DebugWrite("PMM: free=");
    DebugHex64(PhysicalMemoryFreePageCount() << PAGE_SHIFT);
    DebugWrite(" / tracked=");
    DebugHex64(PhysicalMemoryTotalPages() << PAGE_SHIFT);
    DebugWrite("\n");
    return 0;
}
