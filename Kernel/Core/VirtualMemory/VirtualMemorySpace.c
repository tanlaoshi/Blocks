/*
 * VirtualMemorySpace.c — K49：用户页表空间（独立 PML4 + 按需克隆中间表）
 *
 * 【初学者】
 * Create：新 PML4，拷贝内核项（先共享下层）。MapPage 时若踩到共享表则克隆。
 * LoadPageTable：写 CR3。Destroy：只释放本空间跟踪的页。
 */
#include "VirtualMemory.h"
#include "PhysicalMemory.h"
#include "HalSerial.h"
#include "SerialConfig.h"

#if defined(__x86_64__) || defined(_M_X64)
#include "EarlyIdentity.h"

#define PTE_P    PTE_PRESENT
#define PTE_W    PTE_WRITABLE
#define PTE_U    PTE_USER
#define PTE_HUGE (1ull << 7)
#define PTE_ADDR 0x000FFFFFFFFFF000ull

static UINT64 ReadCr3(void) {
    UINT64 Cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(Cr3));
    return Cr3;
}

static void WriteCr3(UINT64 Cr3) {
    __asm__ volatile("mov %0, %%cr3" ::"r"(Cr3) : "memory");
}

static void ZeroPage(void *Page) {
    UINT64 *Q = (UINT64 *)Page;
    UINTN i;
    for (i = 0; i < (PAGE_SIZE / sizeof(UINT64)); i++) {
        Q[i] = 0;
    }
}

static void CopyPage(void *Dst, const void *Src) {
    const UINT64 *S = (const UINT64 *)Src;
    UINT64 *D = (UINT64 *)Dst;
    UINTN i;
    for (i = 0; i < (PAGE_SIZE / sizeof(UINT64)); i++) {
        D[i] = S[i];
    }
}

static UINT64 *TableFromEntry(UINT64 Entry) {
    return (UINT64 *)(UINTN)(Entry & PTE_ADDR);
}

static int SpaceTrack(VIRTUAL_ADDRESS_SPACE *Space, void *Page) {
    if (Space->PageCount >= VM_SPACE_MAX_PAGES) {
        return -1;
    }
    Space->Pages[Space->PageCount++] = Page;
    return 0;
}

static int SpaceOwns(const VIRTUAL_ADDRESS_SPACE *Space, void *Page) {
    int i;
    for (i = 0; i < Space->PageCount; i++) {
        if (Space->Pages[i] == Page) {
            return 1;
        }
    }
    return 0;
}

static void *SpaceAllocPage(VIRTUAL_ADDRESS_SPACE *Space) {
    void *Page = PhysicalMemoryAllocatePage();
    if (Page == 0) {
        return 0;
    }
    ZeroPage(Page);
    if (SpaceTrack(Space, Page) != 0) {
        PhysicalMemoryFreePage(Page);
        return 0;
    }
    return Page;
}

/* 槽空则新建；已是本空间表则用；共享内核表则克隆 */
static int EnsurePrivateTable(VIRTUAL_ADDRESS_SPACE *Space, UINT64 *Slot) {
    void *Page;
    void *Clone;

    if ((*Slot & PTE_P) == 0) {
        Page = SpaceAllocPage(Space);
        if (Page == 0) {
            return -1;
        }
        *Slot = ((UINT64)(UINTN)Page) | PTE_P | PTE_W | PTE_U;
        return 0;
    }
    if ((*Slot & PTE_HUGE) != 0) {
        return -1;
    }
    Page = TableFromEntry(*Slot);
    if (SpaceOwns(Space, Page)) {
        return 0;
    }
    Clone = SpaceAllocPage(Space);
    if (Clone == 0) {
        return -1;
    }
    CopyPage(Clone, Page);
    *Slot = ((UINT64)(UINTN)Clone) | PTE_P | PTE_W | PTE_U;
    return 0;
}

UINT64 VirtualMemoryKernelRoot(void) {
    UINT64 Root = EarlyIdentityRoot();
    if (Root == 0) {
        Root = ReadCr3();
    }
    return Root & PTE_ADDR;
}

void VirtualMemoryLoadPageTable(UINT64 Root) {
    if (Root == 0) {
        return;
    }
    WriteCr3(Root & PTE_ADDR);
}

VIRTUAL_ADDRESS_SPACE *VirtualMemorySpaceCreate(void) {
    VIRTUAL_ADDRESS_SPACE *Space;
    UINT64 *KernelPml4;
    UINT64 *UserPml4;
    UINTN i;
    void *Meta;

    Meta = PhysicalMemoryAllocatePage();
    if (Meta == 0) {
        return 0;
    }
    ZeroPage(Meta);
    Space = (VIRTUAL_ADDRESS_SPACE *)Meta;
    /* 结构放在跟踪页 0；Root 另页 */
    Space->PageCount = 0;
    Space->UserCount = 0;
    if (SpaceTrack(Space, Meta) != 0) {
        PhysicalMemoryFreePage(Meta);
        return 0;
    }
    UserPml4 = (UINT64 *)SpaceAllocPage(Space);
    if (UserPml4 == 0) {
        VirtualMemorySpaceDestroy(Space);
        return 0;
    }
    KernelPml4 = (UINT64 *)(UINTN)VirtualMemoryKernelRoot();
    if (KernelPml4 == 0) {
        VirtualMemorySpaceDestroy(Space);
        return 0;
    }
    for (i = 0; i < 512; i++) {
        UserPml4[i] = KernelPml4[i];
    }
    Space->Root = (UINT64)(UINTN)UserPml4;
    HalSerialWriteChannel(SLOG_MEM, "VMM: user space root ok\n");
    return Space;
}

void VirtualMemorySpaceDestroy(VIRTUAL_ADDRESS_SPACE *Space) {
    int i;
    if (Space == 0) {
        return;
    }
    for (i = 0; i < Space->UserCount; i++) {
        if (Space->UserPhys[i] != 0) {
            PhysicalMemoryFreePage((void *)(UINTN)Space->UserPhys[i]);
            Space->UserPhys[i] = 0;
        }
    }
    Space->UserCount = 0;
    for (i = Space->PageCount - 1; i >= 0; i--) {
        if (Space->Pages[i] != 0) {
            PhysicalMemoryFreePage(Space->Pages[i]);
        }
    }
}

UINT64 VirtualMemorySpaceRoot(const VIRTUAL_ADDRESS_SPACE *Space) {
    return Space != 0 ? Space->Root : 0;
}

int VirtualMemorySpaceMapPage(VIRTUAL_ADDRESS_SPACE *Space, UINT64 Virt,
                              UINT64 Phys, UINT64 Flags) {
    UINT64 *Pml4;
    UINT64 *Pdpt;
    UINT64 *Pd;
    UINT64 *Pt;
    UINTN I4;
    UINTN I3;
    UINTN I2;
    UINTN I1;
    UINT64 F;

    if (Space == 0 || (Virt & (PAGE_SIZE - 1u)) != 0 ||
        (Phys & (PAGE_SIZE - 1u)) != 0) {
        return -1;
    }
    Pml4 = (UINT64 *)(UINTN)(Space->Root & PTE_ADDR);
    if (Pml4 == 0) {
        return -1;
    }
    I4 = (UINTN)((Virt >> 39) & 0x1FFull);
    I3 = (UINTN)((Virt >> 30) & 0x1FFull);
    I2 = (UINTN)((Virt >> 21) & 0x1FFull);
    I1 = (UINTN)((Virt >> 12) & 0x1FFull);

    if (EnsurePrivateTable(Space, &Pml4[I4]) != 0) {
        return -1;
    }
    Pdpt = TableFromEntry(Pml4[I4]);
    if (EnsurePrivateTable(Space, &Pdpt[I3]) != 0) {
        return -1;
    }
    Pd = TableFromEntry(Pdpt[I3]);
    /* 若原是 2MiB 大页，拆成 PT（本刀：拒绝大页槽，要求下层为空或已是表） */
    if ((Pd[I2] & PTE_P) != 0 && (Pd[I2] & PTE_HUGE) != 0) {
        /* 拆大页：新建 PT，填 512 个 4K（恒等）再改目标项 */
        void *NewPt = SpaceAllocPage(Space);
        UINT64 HugePhys;
        UINTN k;
        if (NewPt == 0) {
            return -1;
        }
        HugePhys = Pd[I2] & PTE_ADDR;
        Pt = (UINT64 *)NewPt;
        for (k = 0; k < 512; k++) {
            Pt[k] = (HugePhys + ((UINT64)k << 12)) | PTE_P | PTE_W | PTE_U;
        }
        Pd[I2] = ((UINT64)(UINTN)NewPt) | PTE_P | PTE_W | PTE_U;
    } else if (EnsurePrivateTable(Space, &Pd[I2]) != 0) {
        return -1;
    }
    Pt = TableFromEntry(Pd[I2]);
    F = Flags;
    if ((F & PTE_P) == 0) {
        F |= PTE_P;
    }
    Pt[I1] = (Phys & PTE_ADDR) | F;
    /* 登记用户数据页（Clone 用）；同 Virt 覆盖更新 */
    if ((F & PTE_U) != 0) {
        int u;
        int Slot = -1;
        for (u = 0; u < Space->UserCount; u++) {
            if (Space->UserVirt[u] == Virt) {
                Slot = u;
                break;
            }
        }
        if (Slot < 0 && Space->UserCount < VM_SPACE_USER_MAX) {
            Slot = Space->UserCount++;
            Space->UserVirt[Slot] = Virt;
        }
        if (Slot >= 0) {
            Space->UserPhys[Slot] = Phys & PTE_ADDR;
        }
    }
    return 0;
}

int VirtualMemorySpaceMapRange(VIRTUAL_ADDRESS_SPACE *Space, UINT64 Virt,
                               UINT64 Phys, UINTN Bytes, UINT64 Flags) {
    UINT64 V = Virt;
    UINT64 P = Phys;
    UINTN Left = Bytes;

    if (Space == 0 || Bytes == 0) {
        return -1;
    }
    while (Left > 0) {
        if (VirtualMemorySpaceMapPage(Space, V, P, Flags) != 0) {
            return -1;
        }
        V += PAGE_SIZE;
        P += PAGE_SIZE;
        if (Left <= PAGE_SIZE) {
            break;
        }
        Left -= PAGE_SIZE;
    }
    return 0;
}

VIRTUAL_ADDRESS_SPACE *VirtualMemorySpaceClone(VIRTUAL_ADDRESS_SPACE *Src) {
    VIRTUAL_ADDRESS_SPACE *Dst;
    int i;
    UINT64 Flags = PTE_P | PTE_W | PTE_U;

    if (Src == 0) {
        return 0;
    }
    Dst = VirtualMemorySpaceCreate();
    if (Dst == 0) {
        return 0;
    }
    for (i = 0; i < Src->UserCount; i++) {
        void *NewPhys = PhysicalMemoryAllocatePage();
        if (NewPhys == 0) {
            VirtualMemorySpaceDestroy(Dst);
            return 0;
        }
        CopyPage(NewPhys, (void *)(UINTN)Src->UserPhys[i]);
        if (VirtualMemorySpaceMapPage(Dst, Src->UserVirt[i],
                                      (UINT64)(UINTN)NewPhys, Flags) != 0) {
            PhysicalMemoryFreePage(NewPhys);
            VirtualMemorySpaceDestroy(Dst);
            return 0;
        }
    }
    HalSerialWriteChannel(SLOG_MEM, "VMM: space clone ok\n");
    return Dst;
}

#else /* !x64 */

UINT64 VirtualMemoryKernelRoot(void) {
    return 0;
}

void VirtualMemoryLoadPageTable(UINT64 Root) {
    (void)Root;
}

VIRTUAL_ADDRESS_SPACE *VirtualMemorySpaceCreate(void) {
    return 0;
}

void VirtualMemorySpaceDestroy(VIRTUAL_ADDRESS_SPACE *Space) {
    (void)Space;
}

UINT64 VirtualMemorySpaceRoot(const VIRTUAL_ADDRESS_SPACE *Space) {
    (void)Space;
    return 0;
}

int VirtualMemorySpaceMapPage(VIRTUAL_ADDRESS_SPACE *Space, UINT64 Virt,
                              UINT64 Phys, UINT64 Flags) {
    (void)Space;
    (void)Virt;
    (void)Phys;
    (void)Flags;
    return -1;
}

int VirtualMemorySpaceMapRange(VIRTUAL_ADDRESS_SPACE *Space, UINT64 Virt,
                               UINT64 Phys, UINTN Bytes, UINT64 Flags) {
    (void)Space;
    (void)Virt;
    (void)Phys;
    (void)Bytes;
    (void)Flags;
    return -1;
}

VIRTUAL_ADDRESS_SPACE *VirtualMemorySpaceClone(VIRTUAL_ADDRESS_SPACE *Src) {
    (void)Src;
    return 0;
}

#endif
