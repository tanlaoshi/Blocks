/*
 * HalPageTable.h — 页表根、映射与权限位（x86 位布局见 PageTable.c）
 */
#ifndef HAL_PAGE_TABLE_H
#define HAL_PAGE_TABLE_H

#include "BootTypes.h"

#define HAL_PAGE_PRESENT  (1ULL << 0)
#define HAL_PAGE_WRITABLE (1ULL << 1)
#define HAL_PAGE_USER     (1ULL << 2)

typedef void *(*HalPageAllocateFunction)(void *Ctx);

/* 分页：CPU 当前页表根（x86 为 CR3；其它架构为等价寄存器） */
void HalTlbFlush(UINT64 VirtualAddress);
void HalPageTableLoad(UINT64 Root);
UINT64 HalPageTableGetCurrent(void);
void HalPagingEnable(UINT64 RootPhys);
/* PR-A10：故意触未映射 VA，验收缺页路径（x86 可为空） */
void HalPagingSelfTest(void);

int HalPageKernelSetup(UINTN IdentityMegabytes);
UINT64 HalPageKernelRoot(void);
UINT64 HalPageRootCreate(HalPageAllocateFunction Alloc, void *Ctx);
void HalPageRootCopy(UINT64 DstRoot, UINT64 SrcRoot);
/* 将根表槽 Index 换成私有下一级表（x86：PML4→PDPT）；fork 浅拷贝后必须私有化 */
int HalPagePrivatizeRootSlot(UINT64 Root, UINT32 Index, HalPageAllocateFunction Alloc, void *Ctx);
/*
 * PR-A3：用户地址空间根表准备（x86：私有化槽 0 / PDPT）。
 * Common 不写死根槽号或 PML4[0]。
 */
int HalPagePrepareUserRoot(UINT64 Root, HalPageAllocateFunction Alloc, void *Ctx);
/* PR-A3：COW 软件语义（x86 用 PTE 可用位 bit9；其它 arch 自选布局） */
int HalPageIsCopyOnWrite(UINT64 Pte);
UINT64 HalPageMarkCopyOnWrite(UINT64 Flags); /* 置 COW、清 WRITABLE */
int HalPageMap(UINT64 Root, UINT64 VirtualAddress, UINT64 PhysicalAddress, UINT64 Flags,
               HalPageAllocateFunction Alloc, void *Ctx);
int HalPageUnmapRange(UINT64 Root, UINT64 Start, UINT64 End);
UINT64 HalPageGetEntry(UINT64 Root, UINT64 Virt);
UINT64 HalPageGetEntryCurrent(UINT64 Virt);

#endif
