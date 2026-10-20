/*
 * PhysicalMemory.c — InitializePhysicalMemory（开机模块表）
 *
 * 按开机信息划段，登记位图分配，再打出空闲页数。
 */
#include "KernelModulesPrivate.h"
#include "PhysicalMemory.h"
#include "PhysicalMemoryPrivate.h"
#include "MemoryOps.h"
#include "BootInfo.h"
#include "Debug.h"

int InitializePhysicalMemory(void) {
    return PhysicalMemoryInitialize();
}

int PhysicalMemoryInitialize(void) {
    const BOOT_INFO *Info = BootInfoGet();
    const MEMORY_OPS *Ops;

    if (Info == 0 || Info->RegionCount == 0) {
        return -1;
    }
    PmmPhysLockInit();
    PmmSegmentInit(Info);
#ifdef MEM_BESTFIT
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
