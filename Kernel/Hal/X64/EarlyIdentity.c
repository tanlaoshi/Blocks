/*
 * EarlyIdentity.c — X64 早期 4GiB 恒等映射（静态页表池，不依赖 PMM）
 *
 * 【初学者 · 四级页表（4 级、48 位）在干什么？】
 *   虚址 → PML4 → PDPT → PD →（这里用 2MiB 大页）→ 物理页
 * 我们为 [0, 4GiB) 每 2MiB 建一项「P→V 数值相同」的大页 PTE。
 *
 * 页表页从哪来？
 *   有 PMM 后从分配器拿；当前用本文件 BSS 静态池 gEarlyPtPool[]。
 *
 * 何时调用？
 *   KernelMain 里：Setup() 成功后再 Enable()。栈必须已在窗内
 *   （KernelEntry.S 换过 gEarlyStack）。
 */
#include "EarlyIdentity.h"
#include "IdentityMap.h"
#include "BootTypes.h"

#define PAGE_SIZE        4096u
#define PTE_PRESENT      (1ull << 0)
#define PTE_WRITABLE     (1ull << 1)
#define PTE_HUGE         (1ull << 7)

/* 4GiB / 2MiB = 2048 大页 → 需要 4 张 PD；另加 PML4+PDPT + 余量 */
#define EARLY_PT_PAGES   16

static UINT8 gEarlyPtPool[EARLY_PT_PAGES][PAGE_SIZE] __attribute__((aligned(4096)));
static UINTN gEarlyPtNext;
static UINT64 gKernelRoot;

static void EarlyZero(void *P, UINTN N) {
    UINT8 *B = (UINT8 *)P;
    UINTN i;
    for (i = 0; i < N; i++) {
        B[i] = 0;
    }
}

static void *EarlyAllocPage(void) {
    void *P;
    if (gEarlyPtNext >= EARLY_PT_PAGES) {
        return 0;
    }
    P = gEarlyPtPool[gEarlyPtNext++];
    EarlyZero(P, PAGE_SIZE);
    return P;
}

static UINT64 EarlyPhys(const void *P) {
    return (UINT64)(UINTN)P;
}

/*
 * 建立 [0, IDENTITY_BYTES) 2MiB 大页恒等。
 * 现实现要求 IDENTITY_MB 为 2048 的倍数且 ≤ 512*2048（单 PDPT 槽覆盖 512GB 理论；
 * 此处只填满低 4GiB：4 张 PD）。
 */
int EarlyIdentitySetup(void) {
    UINT64 *Pml4;
    UINT64 *Pdpt;
    UINTN Mb = IDENTITY_MB;
    UINTN HugeTotal;
    UINTN PdCount;
    UINTN PdIndex;
    UINTN HugeIndex;

    if (Mb == 0 || (Mb % 2u) != 0) {
        return -1; /* 须为 2MiB 大页整数倍（以 MB 计即偶数） */
    }

    HugeTotal = (Mb * 1024u * 1024u) / (2u * 1024u * 1024u);
    PdCount = (HugeTotal + 511u) / 512u;
    if (PdCount == 0 || PdCount > 512u) {
        return -1;
    }

    gEarlyPtNext = 0;
    Pml4 = (UINT64 *)EarlyAllocPage();
    Pdpt = (UINT64 *)EarlyAllocPage();
    if (!Pml4 || !Pdpt) {
        return -1;
    }

    Pml4[0] = EarlyPhys(Pdpt) | PTE_PRESENT | PTE_WRITABLE;

    HugeIndex = 0;
    for (PdIndex = 0; PdIndex < PdCount; PdIndex++) {
        UINT64 *Pd = (UINT64 *)EarlyAllocPage();
        UINTN Slot;
        if (!Pd) {
            return -1;
        }
        Pdpt[PdIndex] = EarlyPhys(Pd) | PTE_PRESENT | PTE_WRITABLE;
        for (Slot = 0; Slot < 512u && HugeIndex < HugeTotal; Slot++, HugeIndex++) {
            Pd[Slot] = ((UINT64)HugeIndex << 21) | PTE_PRESENT | PTE_WRITABLE | PTE_HUGE;
        }
    }

    gKernelRoot = EarlyPhys(Pml4);
    return 0;
}

void EarlyIdentityEnable(void) {
    UINT64 Cr4;
    UINT64 Cr0;

    __asm__ volatile("mov %%cr4, %0" : "=r"(Cr4));
    Cr4 |= (1ull << 5); /* PAE */
    __asm__ volatile("mov %0, %%cr4" :: "r"(Cr4));

    __asm__ volatile("mov %0, %%cr3" :: "r"(gKernelRoot) : "memory");

    __asm__ volatile("mov %%cr0, %0" : "=r"(Cr0));
    Cr0 |= (1ull << 31); /* PG */
    __asm__ volatile("mov %0, %%cr0" :: "r"(Cr0) : "memory");
}

UINT64 EarlyIdentityRoot(void) {
    return gKernelRoot;
}
