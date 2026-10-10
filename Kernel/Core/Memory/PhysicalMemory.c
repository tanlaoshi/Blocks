/*
 * PhysicalMemory.c — PR-K4：最小物理页分配器（池编排）
 *
 * 【初学者】
 * 1. 从 BOOT_INFO 里挑一段最大的 Free 区（落在恒等窗内）
 * 2. 位图政策见 PhysicalMemoryBitmap.c（MEMORY_OPS）
 * 3. 公开 Allocate/Free 经 MemoryOpsGet()
 */
#include "PhysicalMemory.h"
#include "PhysicalMemoryPrivate.h"
#include "MemoryOps.h"
#include "BootInfo.h"
#include "IdentityMap.h"
#include "HalSerial.h"
#include "SerialConfig.h"

UINT8 gPhysicalMemoryBitmap[(PHYSICAL_MEMORY_MAX_PAGES + 7u) / 8u];
UINT64 gPhysicalMemoryBase;
UINT32 gPhysicalMemoryPageCount;
UINT32 gPhysicalMemoryFreeCount;
int gPhysicalMemoryReady;

int PhysicalMemoryBitGet(UINT32 Index) {
    return (gPhysicalMemoryBitmap[Index >> 3] >> (Index & 7u)) & 1;
}

void PhysicalMemoryBitSet(UINT32 Index) {
    gPhysicalMemoryBitmap[Index >> 3] |= (UINT8)(1u << (Index & 7u));
}

void PhysicalMemoryBitClear(UINT32 Index) {
    gPhysicalMemoryBitmap[Index >> 3] &= (UINT8)~(1u << (Index & 7u));
}

static void Log(const char *Text) {
    HalSerialWriteChannel(SLOG_MEM, Text);
}

static void LogHex64(UINT64 Value) {
    char Buffer[20];

    HalSerialFormatHex(Buffer, Value, 16);
    HalSerialWriteChannel(SLOG_MEM, Buffer);
}

static void MarkReserved(const BOOT_INFO *Info) {
    UINT32 RegionIndex;
    UINT32 PageIndex;

    for (RegionIndex = 0; RegionIndex < Info->RegionCount; RegionIndex++) {
        const BOOT_MEMORY_REGION *Region = &Info->Regions[RegionIndex];
        UINT64 Start;
        UINT64 End;
        UINT64 PoolEnd =
            gPhysicalMemoryBase + ((UINT64)gPhysicalMemoryPageCount << PAGE_SHIFT);

        if (Region->Free || Region->Size == 0) {
            continue;
        }
        Start = Region->Phys;
        End = Region->Phys + Region->Size;
        if (End <= gPhysicalMemoryBase || Start >= PoolEnd) {
            continue;
        }
        if (Start < gPhysicalMemoryBase) {
            Start = gPhysicalMemoryBase;
        }
        if (End > PoolEnd) {
            End = PoolEnd;
        }
        Start = (Start + (PAGE_SIZE - 1u)) & ~(UINT64)(PAGE_SIZE - 1u);
        End &= ~(UINT64)(PAGE_SIZE - 1u);
        for (; Start < End; Start += PAGE_SIZE) {
            PageIndex = (UINT32)((Start - gPhysicalMemoryBase) >> PAGE_SHIFT);
            if (PageIndex >= gPhysicalMemoryPageCount) {
                break;
            }
            if (!PhysicalMemoryBitGet(PageIndex)) {
                PhysicalMemoryBitSet(PageIndex);
                gPhysicalMemoryFreeCount--;
            }
        }
    }
}

static int PickPool(const BOOT_INFO *Info, UINT64 *OutBase, UINT32 *OutPages) {
    UINT32 Index;
    UINT64 BestBase = 0;
    UINT64 BestSize = 0;

    for (Index = 0; Index < Info->RegionCount; Index++) {
        const BOOT_MEMORY_REGION *Region = &Info->Regions[Index];
        UINT64 Start;
        UINT64 End;
        UINT64 Size;

        if (!Region->Free || Region->Size < PAGE_SIZE) {
            continue;
        }
        Start = (Region->Phys + (PAGE_SIZE - 1u)) & ~(UINT64)(PAGE_SIZE - 1u);
        End = Region->Phys + Region->Size;
        End &= ~(UINT64)(PAGE_SIZE - 1u);
        if (End <= Start) {
            continue;
        }
        if (Start >= IDENTITY_BYTES) {
            continue;
        }
        if (End > IDENTITY_BYTES) {
            End = IDENTITY_BYTES;
        }
        Size = End - Start;
        if (Size > BestSize) {
            BestSize = Size;
            BestBase = Start;
        }
    }
    if (BestSize < PAGE_SIZE) {
        return -1;
    }
    if (BestSize / PAGE_SIZE > PHYSICAL_MEMORY_MAX_PAGES) {
        BestSize = (UINT64)PHYSICAL_MEMORY_MAX_PAGES * PAGE_SIZE;
    }
    *OutBase = BestBase;
    *OutPages = (UINT32)(BestSize / PAGE_SIZE);
    return 0;
}

/*
 * PhysicalMemoryInitialize — 从 BOOT_INFO 建位图池并注册 MEMORY_OPS
 *
 * 做什么：挑最大 Free 区、MarkReserved、MemoryOpsRegister(BitmapOps)。
 * 谁调用：MemoryInitialize。
 * 前后文：前 — BootInfoSave；后 — PhysicalMemoryAllocatePage。
 * 返回：0 成功；-1 无池或全保留。
 */
int PhysicalMemoryInitialize(void) {
    const BOOT_INFO *Info = BootInfoGet();
    UINT32 Index;

    gPhysicalMemoryReady = 0;
    gPhysicalMemoryBase = 0;
    gPhysicalMemoryPageCount = 0;
    gPhysicalMemoryFreeCount = 0;
    if (Info == 0 || Info->RegionCount == 0) {
        Log("PMM: no BootInfo regions\n");
        return -1;
    }
    if (PickPool(Info, &gPhysicalMemoryBase, &gPhysicalMemoryPageCount) != 0) {
        Log("PMM: no free pool in identity window\n");
        return -1;
    }
    for (Index = 0; Index < sizeof(gPhysicalMemoryBitmap); Index++) {
        gPhysicalMemoryBitmap[Index] = 0;
    }
    gPhysicalMemoryFreeCount = gPhysicalMemoryPageCount;
    MarkReserved(Info);
    if (gPhysicalMemoryFreeCount == 0) {
        Log("PMM: pool fully reserved\n");
        gPhysicalMemoryReady = 0;
        return -1;
    }
    gPhysicalMemoryReady = 1;

    Log("PMM: pool=");
    LogHex64(gPhysicalMemoryBase);
    Log(" pages=");
    LogHex64(gPhysicalMemoryPageCount);
    Log(" free=");
    LogHex64(gPhysicalMemoryFreeCount);
    Log("\n");

    MemoryOpsRegister(MemoryBitmapOps());
    if (MemoryOpsGet() != 0 && MemoryOpsGet()->Init != 0) {
        MemoryOpsGet()->Init();
    }
    Log("MemoryOps: bitmap ok\n");
    return 0;
}

void *PhysicalMemoryAllocatePages(UINT32 Count) {
    const MEMORY_OPS *Ops = MemoryOpsGet();
    if (Ops == 0 || Ops->AllocPagesLocked == 0) {
        return 0;
    }
    return Ops->AllocPagesLocked(Count);
}

void *PhysicalMemoryAllocatePage(void) {
    return PhysicalMemoryAllocatePages(1);
}

void PhysicalMemoryFreePages(void *Page, UINT32 Count) {
    const MEMORY_OPS *Ops = MemoryOpsGet();
    if (Ops == 0 || Ops->FreePagesLocked == 0) {
        return;
    }
    Ops->FreePagesLocked(Page, Count);
}

void PhysicalMemoryFreePage(void *Page) {
    PhysicalMemoryFreePages(Page, 1);
}

int PhysicalMemoryRetainPage(void *Page) {
    const MEMORY_OPS *Ops = MemoryOpsGet();
    if (Ops == 0 || Ops->RetainPageLocked == 0) {
        return -1;
    }
    return Ops->RetainPageLocked(Page);
}

void PhysicalMemoryReleasePage(void *Page) {
    const MEMORY_OPS *Ops = MemoryOpsGet();
    if (Ops == 0 || Ops->ReleasePageLocked == 0) {
        return;
    }
    Ops->ReleasePageLocked(Page);
}

UINT64 PhysicalMemoryTotalPages(void) {
    return gPhysicalMemoryReady ? gPhysicalMemoryPageCount : 0;
}

UINT64 PhysicalMemoryFreePageCount(void) {
    return gPhysicalMemoryReady ? gPhysicalMemoryFreeCount : 0;
}

void *HalDmaAllocatePages(UINT32 Count) {
    return PhysicalMemoryAllocatePages(Count);
}
