/*
 * PhysicalMemory.c — PR-K4：最小物理页分配器（单区位图）
 *
 * 【初学者】
 * 1. 从 BOOT_INFO 里挑一段最大的 Free 区（落在恒等窗内）
 * 2. 每位代表一页：0=空闲，1=已用
 * 3. AllocatePages：first-fit 找连续 Count 个 0 并置 1
 *
 * 不做：多段合并、Ops 可替换、自旋锁（单核 boot 够用）。
 * 保留区（内核/FB）已在 Handoff 标 Free=0，不会进本池。
 */
#include "PhysicalMemory.h"
#include "BootInfo.h"
#include "IdentityMap.h"
#include "HalSerial.h"
#include "ToySerialConfig.h"

/* 最多跟踪 128MiB → 32768 页 → 4KiB 位图 */
#define PMM_MAX_PAGES 32768u

static UINT8 gBitmap[(PMM_MAX_PAGES + 7u) / 8u];
static UINT64 gBase;      /* 池物理起点（页对齐） */
static UINT32 gPageCount; /* 池内页数 */
static UINT32 gFreeCount;
static int gReady;

static int BitGet(UINT32 Index) {
    return (gBitmap[Index >> 3] >> (Index & 7u)) & 1;
}

static void BitSet(UINT32 Index) {
    gBitmap[Index >> 3] |= (UINT8)(1u << (Index & 7u));
}

static void BitClear(UINT32 Index) {
    gBitmap[Index >> 3] &= (UINT8)~(1u << (Index & 7u));
}

static void PmmLog(const char *Text) {
    HalSerialWriteChannel(TOY_SLOG_MEM, Text);
}

static void PmmLogHex64(UINT64 Value) {
    char Buf[20];

    HalSerialFormatHex(Buf, Value, 16);
    HalSerialWriteChannel(TOY_SLOG_MEM, Buf);
}

/* Handoff 会另挂 Free=0 保留段；从池里抠掉与之重叠的页 */
static void MarkReserved(const BOOT_INFO *Info) {
    UINT32 r;
    UINT32 i;

    for (r = 0; r < Info->RegionCount; r++) {
        const BOOT_MEMORY_REGION *R = &Info->Regions[r];
        UINT64 Start;
        UINT64 End;
        UINT64 PoolEnd = gBase + ((UINT64)gPageCount << PAGE_SHIFT);

        if (R->Free || R->Size == 0) {
            continue;
        }
        Start = R->Phys;
        End = R->Phys + R->Size;
        if (End <= gBase || Start >= PoolEnd) {
            continue;
        }
        if (Start < gBase) {
            Start = gBase;
        }
        if (End > PoolEnd) {
            End = PoolEnd;
        }
        Start = (Start + (PAGE_SIZE - 1u)) & ~(UINT64)(PAGE_SIZE - 1u);
        End &= ~(UINT64)(PAGE_SIZE - 1u);
        for (; Start < End; Start += PAGE_SIZE) {
            i = (UINT32)((Start - gBase) >> PAGE_SHIFT);
            if (i >= gPageCount) {
                break;
            }
            if (!BitGet(i)) {
                BitSet(i);
                gFreeCount--;
            }
        }
    }
}

static int PickPool(const BOOT_INFO *Info, UINT64 *OutBase, UINT32 *OutPages) {
    UINT32 i;
    UINT64 BestBase = 0;
    UINT64 BestSize = 0;

    for (i = 0; i < Info->RegionCount; i++) {
        const BOOT_MEMORY_REGION *R = &Info->Regions[i];
        UINT64 Start;
        UINT64 End;
        UINT64 Size;

        if (!R->Free || R->Size < PAGE_SIZE) {
            continue;
        }
        Start = (R->Phys + (PAGE_SIZE - 1u)) & ~(UINT64)(PAGE_SIZE - 1u);
        End = R->Phys + R->Size;
        End &= ~(UINT64)(PAGE_SIZE - 1u);
        if (End <= Start) {
            continue;
        }
        /* 只管理恒等窗内，便于 K4 直接解引用 */
        if (Start >= TOY_IDENTITY_BYTES) {
            continue;
        }
        if (End > TOY_IDENTITY_BYTES) {
            End = TOY_IDENTITY_BYTES;
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
    if (BestSize / PAGE_SIZE > PMM_MAX_PAGES) {
        BestSize = (UINT64)PMM_MAX_PAGES * PAGE_SIZE;
    }
    *OutBase = BestBase;
    *OutPages = (UINT32)(BestSize / PAGE_SIZE);
    return 0;
}

int PhysicalMemoryInitialize(void) {
    const BOOT_INFO *Info = BootInfoGet();
    UINT32 i;

    gReady = 0;
    gBase = 0;
    gPageCount = 0;
    gFreeCount = 0;
    if (Info == 0 || Info->RegionCount == 0) {
        PmmLog("PMM: no BootInfo regions\n");
        return -1;
    }
    if (PickPool(Info, &gBase, &gPageCount) != 0) {
        PmmLog("PMM: no free pool in identity window\n");
        return -1;
    }
    for (i = 0; i < sizeof(gBitmap); i++) {
        gBitmap[i] = 0;
    }
    gFreeCount = gPageCount;
    MarkReserved(Info);
    if (gFreeCount == 0) {
        PmmLog("PMM: pool fully reserved\n");
        gReady = 0;
        return -1;
    }
    gReady = 1;

    PmmLog("PMM: pool=");
    PmmLogHex64(gBase);
    PmmLog(" pages=");
    PmmLogHex64(gPageCount);
    PmmLog(" free=");
    PmmLogHex64(gFreeCount);
    PmmLog("\n");
    return 0;
}

void *PhysicalMemoryAllocatePages(UINT32 Count) {
    UINT32 Start;
    UINT32 i;
    UINT32 Run;

    if (!gReady || Count == 0 || Count > gPageCount || Count > gFreeCount) {
        return 0;
    }
    Run = 0;
    for (Start = 0; Start < gPageCount; Start++) {
        if (BitGet(Start)) {
            Run = 0;
            continue;
        }
        Run++;
        if (Run < Count) {
            continue;
        }
        /* [Start - Count + 1, Start] */
        {
            UINT32 First = Start + 1u - Count;
            for (i = 0; i < Count; i++) {
                BitSet(First + i);
            }
            gFreeCount -= Count;
            return (void *)(UINTN)(gBase + ((UINT64)First << PAGE_SHIFT));
        }
    }
    return 0;
}

void *PhysicalMemoryAllocatePage(void) {
    return PhysicalMemoryAllocatePages(1);
}

void PhysicalMemoryFreePages(void *Page, UINT32 Count) {
    UINT64 Phys;
    UINT32 First;
    UINT32 i;

    if (!gReady || Page == 0 || Count == 0) {
        return;
    }
    Phys = (UINT64)(UINTN)Page;
    if (Phys < gBase || (Phys - gBase) % PAGE_SIZE != 0) {
        return;
    }
    First = (UINT32)((Phys - gBase) >> PAGE_SHIFT);
    if (First >= gPageCount || Count > gPageCount - First) {
        return;
    }
    for (i = 0; i < Count; i++) {
        if (BitGet(First + i)) {
            BitClear(First + i);
            gFreeCount++;
        }
    }
}

void PhysicalMemoryFreePage(void *Page) {
    PhysicalMemoryFreePages(Page, 1);
}

int PhysicalMemoryRetainPage(void *Page) {
    if (!gReady || Page == 0) {
        return -1;
    }
    return 0;
}

void PhysicalMemoryReleasePage(void *Page) {
    PhysicalMemoryFreePage(Page);
}

UINT64 PhysicalMemoryTotalPages(void) {
    return gReady ? gPageCount : 0;
}

UINT64 PhysicalMemoryFreePageCount(void) {
    return gReady ? gFreeCount : 0;
}
