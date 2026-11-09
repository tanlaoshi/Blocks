/*
 * PageTableWalk.c — HAL/RiscV 页表遍历与辅助（PR-S3-pagetable-1）
 */
/*
 * HAL/RiscV/PageTable.c — PR-A10：真 MMU（Sv39 satp）+ 缺页陷阱
 * Common 仍见 HAL_PAGE_*；GetEntry 返回规范化 PTE。
 */
#include "Hal.h"
#include "PhysicalMemory.h"
#include "BoardConfig.h"

#include "PageTablePrivate.h"

UINT64 PagePhys(const void *Ptr) {
    return (UINT64)(UINTN)Ptr;
}

void PageZero(void *Ptr, UINTN Size) {
    UINT8 *B = (UINT8 *)Ptr;
    for (UINTN i = 0; i < Size; i++) {
        B[i] = 0;
    }
}

void *PageAllocTable(void) {
    void *Page = PhysicalMemoryAllocatePage();
    if (!Page) {
        return 0;
    }
    PageZero(Page, PAGE_SIZE);
    return Page;
}

UINT64 NativeFromHal(UINT64 Phys, UINT64 HalFlags) {
    UINT64 N = PTE_V | PTE_A | PTE_D | ((Phys >> 12) << PTE_PPN_SHIFT);
    /* 内核可执行代码也在恒等映射内：叶子给 R|W|X */
    N |= PTE_R | PTE_X;
    /* 动态映射页来自 PMM（DRAM）：C906 须 Cacheable，否则 AMO cause=7 */
    N |= PTE_THEAD_RAM;
    if (HalFlags & HAL_PAGE_WRITABLE) {
        N |= PTE_W;
    }
    if (HalFlags & HAL_PAGE_USER) {
        N |= PTE_U;
        /* 用户页一般不给 X 除非代码；此处与 x86 一致仅按 Flags */
        if (!(HalFlags & HAL_PAGE_WRITABLE)) {
            /* RO 用户：仍可读 */
        }
    }
    if (HalFlags & HAL_PAGE_COPY_ON_WRITE) {
        N |= PTE_RSW_COW;
        N &= ~PTE_W;
    }
    return N;
}

UINT64 HalViewFromNative(UINT64 Native) {
    UINT64 Out;
    if (!(Native & PTE_V)) {
        return 0;
    }
    /* 非叶子（仅 V）：不当作 present 映射页 */
    if (!(Native & (PTE_R | PTE_W | PTE_X))) {
        return 0;
    }
    Out = ((Native >> PTE_PPN_SHIFT) << 12) | HAL_PAGE_PRESENT;
    if (Native & PTE_W) {
        Out |= HAL_PAGE_WRITABLE;
    }
    if (Native & PTE_U) {
        Out |= HAL_PAGE_USER;
    }
    if (Native & PTE_RSW_COW) {
        Out |= HAL_PAGE_COPY_ON_WRITE;
    }
    return Out;
}

int IsLeaf(UINT64 Pte) {
    return (Pte & PTE_V) && (Pte & (PTE_R | PTE_W | PTE_X));
}

extern void VirtualMemoryForgetPage(void *Ctx, void *Page);

static int TableEmpty(const UINT64 *Table) {
    int i;

    for (i = 0; i < 512; i++) {
        if (Table[i] & PTE_V) {
            return 0;
        }
    }
    return 1;
}

static void DropNewTable(UINT64 *Parent, UINT64 Index, UINT64 *Table,
                         HalPageAllocateFunction Alloc, void *Ctx) {
    if (!Table || !TableEmpty(Table)) {
        return;
    }
    Parent[Index] = 0;
    if (Alloc) {
        VirtualMemoryForgetPage(Ctx, Table);
    }
    PhysicalMemoryFreePage(Table);
}

/*
 * 把 L1 上的 2MiB megapage 拆成 L0（512×4KiB），保留原 leaf 属性。
 * 对齐 Arm64 SplitBlockL2：Create 遇 megapage 须拆表，否则 MapIdentity(FB) 永挂
 *（HalPageKernelSetup 用 2MiB 恒等，ramfb 落在 DRAM 内必撞 leaf）。
 */
static UINT64 *SplitMegapageL1(UINT64 *L1, UINT64 Index1,
                               HalPageAllocateFunction Alloc, void *Ctx) {
    UINT64 Old;
    UINT64 PaBase;
    UINT64 Attr;
    UINT64 *L0;
    UINTN i;

    Old = L1[Index1];
    if (!IsLeaf(Old)) {
        return (UINT64 *)(UINTN)(((Old >> PTE_PPN_SHIFT) << 12));
    }
    L0 = Alloc ? (UINT64 *)Alloc(Ctx) : (UINT64 *)PageAllocTable();
    if (!L0) {
        return 0;
    }
    if (!Alloc) {
        PageZero(L0, PAGE_SIZE);
    }
    /* Sv39 2MiB leaf：PPN 指向 2MiB 对齐 PA；低 9 位 PPN 须为 0 */
    PaBase = (Old >> PTE_PPN_SHIFT) << 12;
    Attr = Old & ((1ULL << PTE_PPN_SHIFT) - 1ULL); /* V/R/W/X/U/G/A/D/RSW */
    Attr |= Old & 0xF800000000000000ULL;           /* T-Head MAEE [63:59] */
    for (i = 0; i < 512; i++) {
        L0[i] = Attr | (((PaBase + ((UINT64)i << 12)) >> 12) << PTE_PPN_SHIFT);
    }
    L1[Index1] = PTE_V | ((PagePhys(L0) >> 12) << PTE_PPN_SHIFT);
    return L0;
}

/* Sv39：3 级 VPN[2:0] */
UINT64 *PageWalk(UINT64 *L2, UINT64 Virt, int Create,
                        HalPageAllocateFunction Alloc, void *Ctx) {
    UINT64 i2 = (Virt >> 30) & 0x1FF;
    UINT64 i1 = (Virt >> 21) & 0x1FF;
    UINT64 i0 = (Virt >> 12) & 0x1FF;
    UINT64 *L1 = 0;
    UINT64 *L0;
    int MadeL1 = 0;

    if (!(L2[i2] & PTE_V)) {
        if (!Create) {
            return 0;
        }
        L1 = Alloc ? (UINT64 *)Alloc(Ctx) : (UINT64 *)PageAllocTable();
        if (!L1) {
            return 0;
        }
        if (!Alloc) {
            PageZero(L1, PAGE_SIZE);
        }
        L2[i2] = PTE_V | ((PagePhys(L1) >> 12) << PTE_PPN_SHIFT);
        MadeL1 = 1;
    } else if (IsLeaf(L2[i2])) {
        /* 本柱不用 1GiB L2 leaf；遇则拒 */
        return 0;
    }
    L1 = (UINT64 *)(UINTN)(((L2[i2] >> PTE_PPN_SHIFT) << 12));

    if (!(L1[i1] & PTE_V)) {
        if (!Create) {
            return 0;
        }
        L0 = Alloc ? (UINT64 *)Alloc(Ctx) : (UINT64 *)PageAllocTable();
        if (!L0) {
            if (MadeL1) {
                DropNewTable(L2, i2, L1, Alloc, Ctx);
            }
            return 0;
        }
        if (!Alloc) {
            PageZero(L0, PAGE_SIZE);
        }
        L1[i1] = PTE_V | ((PagePhys(L0) >> 12) << PTE_PPN_SHIFT);
    } else if (IsLeaf(L1[i1])) {
        if (!Create) {
            return 0;
        }
        L0 = SplitMegapageL1(L1, i1, Alloc, Ctx);
        if (!L0) {
            if (MadeL1) {
                DropNewTable(L2, i2, L1, Alloc, Ctx);
            }
            return 0;
        }
        return &L0[i0];
    }
    L0 = (UINT64 *)(UINTN)(((L1[i1] >> PTE_PPN_SHIFT) << 12));
    return &L0[i0];
}

UINT64 *PageLookup(UINT64 Root, UINT64 Virt) {
    UINT64 *L2 = (UINT64 *)(UINTN)(Root & ~0xFFFULL);
    return PageWalk(L2, Virt, 0, 0, 0);
}

