/*
 * VirtualMemorySpace.c — K49：用户页表空间（独立 PML4 + 按需克隆中间表）
 *
 * 【初学者】
 * - Core/VirtualMemory：Process/ELF 用户态页表；内核根见 VirtualMemoryKernelRoot。
 * - 入口：VirtualMemorySpaceCreate/Destroy/MapPage/MapRange/Clone、VirtualMemoryLoadPageTable。
 * - 边界：仅 X64 实现；非 x64 为桩。不替代 VirtualMemory.c 的 MMIO Map。
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

/*
 * EnsurePrivateTable — Map 前保证页表槽属本 Space
 *
 * 做什么：空槽分配页表；已属 Space 则 OK；共享内核表则克隆一页。
 * 谁调用：仅 VirtualMemorySpaceMapPage。
 * 返回：0 成功；-1 OOM 或大页槽。
 */
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

/*
 * VirtualMemoryKernelRoot — 当前内核 PML4 物理址
 *
 * 做什么：EarlyIdentityRoot 或读 CR3，掩 PTE_ADDR。
 * 谁调用：VirtualMemorySpaceCreate（拷贝内核项）、ProcessExecPath（恢复 CR3）。
 * 返回：0 表示不可用。
 */
UINT64 VirtualMemoryKernelRoot(void) {
    UINT64 Root = EarlyIdentityRoot();
    if (Root == 0) {
        Root = ReadCr3();
    }
    return Root & PTE_ADDR;
}

/*
 * VirtualMemoryLoadPageTable — 切换 CR3
 *
 * 做什么：mov Root→cr3；Root=0 忽略。
 * 谁调用：ProcessExecPath（进用户态/回内核）、ProcessFork 保存 parent CR3 上下文。
 */
void VirtualMemoryLoadPageTable(UINT64 Root) {
    if (Root == 0) {
        return;
    }
    WriteCr3(Root & PTE_ADDR);
}

/*
 * VirtualMemorySpaceCreate — 新用户地址空间
 *
 * 做什么：分配 VIRTUAL_ADDRESS_SPACE + 用户 PML4，512 项拷贝内核 PML4。
 * 谁调用：ProcessExecPath、VirtualMemorySpaceClone。
 * 前后文：后 — ElfLoader MapRange、VirtualMemoryLoadPageTable。
 * 返回：Space 指针；失败 NULL（已释放部分页）。
 */
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

/*
 * VirtualMemorySpaceDestroy — 释放空间跟踪的全部页与用户数据页
 *
 * 谁调用：ProcessExecPath 失败清理、ProcessFork 丢弃子空间、Clone 失败路径。
 */
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

/*
 * VirtualMemorySpaceRoot — 用户 PML4 物理址（写 CR3 用）
 *
 * 谁调用：ProcessExecPath、ProcessFork、ElfLoader 注释链。
 */
UINT64 VirtualMemorySpaceRoot(const VIRTUAL_ADDRESS_SPACE *Space) {
    return Space != 0 ? Space->Root : 0;
}

/*
 * VirtualMemorySpaceMapPage — 4KiB 映射（按需私有中间表）
 *
 * 做什么：Walk PML4→PT；共享内核表则 EnsurePrivateTable 克隆；可拆 2MiB 大页。
 * 谁调用：VirtualMemorySpaceMapRange、VirtualMemorySpaceClone、ElfLoader。
 * 前后文：前 — PhysicalMemoryAllocatePage 供用户数据；Flags 含 PTE_U 时登记 UserVirt/Phys。
 * 返回：0 成功；-1 参数/OOM/大页冲突。
 */
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

/*
 * VirtualMemorySpaceMapRange — 按页步进 MapPage
 *
 * 谁调用：ElfLoader 映射 PT_LOAD 与用户栈。
 * 返回：0 成功；-1 任一页失败。
 */
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

/*
 * VirtualMemorySpaceClone — fork：新空间 + 拷贝已登记用户页内容
 *
 * 做什么：Create 后逐 UserVirt 分配页、memcpy、MapPage。
 * 谁调用：ProcessFork（ProcessFork.c）。
 * 返回：新 Space；失败 NULL 并 Destroy 部分结果。
 */
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

/* 下列 API 在非 X64 为桩：用户 ELF 页表本刀未启用。 */

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
