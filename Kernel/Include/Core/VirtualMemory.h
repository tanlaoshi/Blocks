/*
 * VirtualMemory.h — 虚拟内存门面
 *
 * 【初学者】
 * X64 上 EarlyIdentity 已打开 4GiB 恒等分页。
 * K5：认领 PG / FB；K13：MapMmio；K49：用户 AddressSpace（独立 PML4 + 切 CR3）。
 */
#ifndef VIRTUAL_MEMORY_H
#define VIRTUAL_MEMORY_H

#include "BootTypes.h"

#define PTE_PRESENT  (1ull << 0)
#define PTE_WRITABLE (1ull << 1)
#define PTE_USER     (1ull << 2)

#define VM_SPACE_MAX_PAGES 64

typedef struct {
    UINT64 Root;
    void *Pages[VM_SPACE_MAX_PAGES];
    int PageCount;
} VIRTUAL_ADDRESS_SPACE;

int VirtualMemoryInitialize(void);
void VirtualMemoryEnable(void);
int VirtualMemoryMapIdentity(UINT64 Phys, UINT64 Size);
int VirtualMemoryMapMmio(UINT64 Phys, UINT64 Size, UINT64 *OutVirt);

UINT64 VirtualMemoryKernelRoot(void);
void VirtualMemoryLoadPageTable(UINT64 Root);

VIRTUAL_ADDRESS_SPACE *VirtualMemorySpaceCreate(void);
void VirtualMemorySpaceDestroy(VIRTUAL_ADDRESS_SPACE *Space);
UINT64 VirtualMemorySpaceRoot(const VIRTUAL_ADDRESS_SPACE *Space);
/* 4KiB：把 Virt → Phys 映进 Space（写时克隆与内核共享的中间表） */
int VirtualMemorySpaceMapPage(VIRTUAL_ADDRESS_SPACE *Space, UINT64 Virt,
                              UINT64 Phys, UINT64 Flags);
int VirtualMemorySpaceMapRange(VIRTUAL_ADDRESS_SPACE *Space, UINT64 Virt,
                               UINT64 Phys, UINTN Bytes, UINT64 Flags);

#endif
