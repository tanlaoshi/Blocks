/*
 * VirtualMemory.h — 虚拟内存门面
 *
 * 【初学者】
 * X64 上 EarlyIdentity 已打开 4GiB 恒等分页。
 * K5：认领 PG / FB；K13：MapMmio；K49：用户 AddressSpace；K50：SpaceClone。
 */
#ifndef VIRTUAL_MEMORY_H
#define VIRTUAL_MEMORY_H

#include "BootTypes.h"

#define PTE_PRESENT  (1ull << 0)
#define PTE_WRITABLE (1ull << 1)
#define PTE_USER     (1ull << 2)

#define VM_SPACE_MAX_PAGES 64
#define VM_SPACE_USER_MAX  48

typedef struct {
    UINT64 Root;
    void *Pages[VM_SPACE_MAX_PAGES];
    int PageCount;
    /* MapPage 登记的用户数据页（供 Clone eager copy） */
    UINT64 UserVirt[VM_SPACE_USER_MAX];
    UINT64 UserPhys[VM_SPACE_USER_MAX];
    int UserCount;
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
int VirtualMemorySpaceMapPage(VIRTUAL_ADDRESS_SPACE *Space, UINT64 Virt,
                              UINT64 Phys, UINT64 Flags);
int VirtualMemorySpaceMapRange(VIRTUAL_ADDRESS_SPACE *Space, UINT64 Virt,
                               UINT64 Phys, UINTN Bytes, UINT64 Flags);
/* K50：新空间 + 用户数据页物理拷贝（非 PTE COW） */
VIRTUAL_ADDRESS_SPACE *VirtualMemorySpaceClone(VIRTUAL_ADDRESS_SPACE *Src);

#endif
