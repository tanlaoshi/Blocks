/*
 * PageTablePrivate.h — HAL/RiscV PageTableWalk / PageTable 内部交接（PR-S3-pagetable-1）
 */
#ifndef PAGE_TABLE_PRIVATE_H
#define PAGE_TABLE_PRIVATE_H

#include "Hal.h"

#define PTE_V (1ULL << 0)
#define PTE_R (1ULL << 1)
#define PTE_W (1ULL << 2)
#define PTE_X (1ULL << 3)
#define PTE_U (1ULL << 4)
#define PTE_G (1ULL << 5)
#define PTE_A (1ULL << 6)
#define PTE_D (1ULL << 7)
#define PTE_RSW_COW (1ULL << 8) /* RSW 软件位 */

#define PTE_PPN_SHIFT 10
#define SATP_MODE_SV39 (8ULL << 60)

/*
 * T-Head C906 MAEE：叶子 PTE[63:59] 内存属性（非标准 Svpbmt）。
 * AMO 要求 Cacheable（bit62）；否则 Store/AMO access fault（cause=7）。
 * 对齐 Linux _PAGE_PMA_THEAD / NuttX MMU_THEAD_*；virt 无 MAEE → 0。
 */
#if defined(BOARD_IS_VIRT) && BOARD_IS_VIRT
#define PTE_THEAD_RAM 0ULL
#define PTE_THEAD_DEV 0ULL
#else
#define PTE_THEAD_RAM ((1ULL << 62) | (1ULL << 61) | (1ULL << 60)) /* C|B|SH */
#define PTE_THEAD_DEV ((1ULL << 63) | (1ULL << 60))               /* SO|SH */
#endif

#define HAL_PAGE_COPY_ON_WRITE (1ULL << 9)

extern UINT64 gKernelRoot;
extern UINT64 gCurrentRoot;
extern int gMmuOn;
UINT64 NativeFromHal(UINT64 Phys, UINT64 HalFlags);
UINT64 HalViewFromNative(UINT64 Native);
int IsLeaf(UINT64 Pte);
UINT64 PagePhys(const void *Ptr);
void PageZero(void *Ptr, UINTN Size);
void *PageAllocTable(void);
UINT64 *PageWalk(UINT64 *L2, UINT64 Virt, int Create,
                 HalPageAllocateFunction Alloc, void *Ctx);
UINT64 *PageLookup(UINT64 Root, UINT64 Virt);

#endif
