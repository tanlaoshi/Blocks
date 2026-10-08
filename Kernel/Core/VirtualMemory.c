/*
 * VirtualMemory.c — K5 认领恒等分页；K13 MapMmio（高址 BAR）
 *
 * 【初学者】
 * X64：KernelMain 已 EarlyIdentityEnable（[0,4GiB) 2MiB 大页）。
 * 窗外 MMIO（如 QEMU xHCI @0x800000000）须往现有 PML4 里补 PDPT/PD 项，
 * 否则解引用会缺页卡住。本刀用 2MiB 大页、虚址=物理址、PCD|PWT。
 *
 * 【积木】框架壳；完整页表 Ops / 用户空间另刀。
 */
#include "VirtualMemory.h"
#include "BootInfo.h"
#include "IdentityMap.h"
#include "PhysicalMemory.h"
#include "HalSerial.h"
#include "ToySerialConfig.h"

#if defined(__x86_64__) || defined(_M_X64)
#include "EarlyIdentity.h"
#endif

static int gVmReady;

static void VmLog(const char *Text) {
    HalSerialWriteChannel(TOY_SLOG_MEM, Text);
}

#if defined(__x86_64__) || defined(_M_X64)
#define PTE_P     (1ull << 0)
#define PTE_W     (1ull << 1)
#define PTE_HUGE  (1ull << 7)
#define PTE_ADDR  0x000FFFFFFFFFF000ull
#define HUGE_SHIFT 21
#define HUGE_SIZE  (1ull << HUGE_SHIFT)

static void VmLogHex64(UINT64 Value) {
    char Buf[20];

    HalSerialFormatHex(Buf, Value, 16);
    HalSerialWriteChannel(TOY_SLOG_MEM, Buf);
}

static int Cr0PagingOn(void) {
    UINT64 Cr0;

    __asm__ volatile("mov %%cr0, %0" : "=r"(Cr0));
    return (Cr0 & (1ull << 31)) ? 1 : 0;
}

static UINT64 ReadCr3(void) {
    UINT64 Cr3;

    __asm__ volatile("mov %%cr3, %0" : "=r"(Cr3));
    return Cr3;
}

static void TlbFlushAll(void) {
    UINT64 Cr3 = ReadCr3();

    __asm__ volatile("mov %0, %%cr3" :: "r"(Cr3) : "memory");
}

static void ZeroPage(void *Page) {
    UINT64 *Q = (UINT64 *)Page;
    UINTN i;

    for (i = 0; i < (PAGE_SIZE / sizeof(UINT64)); i++) {
        Q[i] = 0;
    }
}

static UINT64 *TableFromEntry(UINT64 Entry) {
    return (UINT64 *)(UINTN)(Entry & PTE_ADDR);
}

/* 槽空则分配一张页表；已是表项则成功；误为大页则失败 */
static int EnsureNextTable(UINT64 *Slot) {
    void *Page;

    if ((*Slot & PTE_P) != 0) {
        if ((*Slot & PTE_HUGE) != 0) {
            return -1;
        }
        return 0;
    }
    Page = PhysicalMemoryAllocatePage();
    if (Page == 0) {
        return -1;
    }
    ZeroPage(Page);
    *Slot = ((UINT64)(UINTN)Page) | PTE_P | PTE_W;
    return 0;
}

/* 映一个对齐的 2MiB：Virt=Phys；已正确映过则空操作 */
static int MapHugeIdentityMmio(UINT64 PhysAligned) {
    UINT64 Virt = PhysAligned;
    UINT64 *Pml4;
    UINT64 *Pdpt;
    UINT64 *Pd;
    UINTN I4;
    UINTN I3;
    UINTN I2;
    UINT64 Want;
    UINT64 Root;

    Root = EarlyIdentityRoot();
    if (Root == 0) {
        Root = ReadCr3();
    }
    Pml4 = (UINT64 *)(UINTN)(Root & PTE_ADDR);
    if (Pml4 == 0) {
        return -1;
    }

    I4 = (UINTN)((Virt >> 39) & 0x1FFull);
    I3 = (UINTN)((Virt >> 30) & 0x1FFull);
    I2 = (UINTN)((Virt >> 21) & 0x1FFull);

    if (EnsureNextTable(&Pml4[I4]) != 0) {
        return -1;
    }
    Pdpt = TableFromEntry(Pml4[I4]);
    if (EnsureNextTable(&Pdpt[I3]) != 0) {
        return -1;
    }
    Pd = TableFromEntry(Pdpt[I3]);

    /* 先 P|W|HUGE；UC（PCD|PWT）后刀再钉 */
    Want = PhysAligned | PTE_P | PTE_W | PTE_HUGE;
    if ((Pd[I2] & PTE_P) != 0) {
        if ((Pd[I2] & PTE_HUGE) == 0) {
            return -1;
        }
        if ((Pd[I2] & PTE_ADDR) != (PhysAligned & PTE_ADDR)) {
            return -1;
        }
        return 0;
    }
    Pd[I2] = Want;
    return 0;
}
#endif

int VirtualMemoryMapIdentity(UINT64 Phys, UINT64 Size) {
    if (Size == 0) {
        return 0;
    }
    if (Phys >= TOY_IDENTITY_BYTES) {
        return -1;
    }
    if (Size > TOY_IDENTITY_BYTES - Phys) {
        return -1;
    }
    return 0;
}

int VirtualMemoryMapMmio(UINT64 Phys, UINT64 Size, UINT64 *OutVirt) {
#if defined(__x86_64__) || defined(_M_X64)
    UINT64 End;
    UINT64 Cur;
    UINT64 Mapped = 0;

    if (Size == 0 || Phys == 0) {
        return -1;
    }
    if (Phys > ~0ull - Size + 1ull) {
        return -1;
    }
    End = Phys + Size;

    /* 窗内已恒等，直接返回物理=虚址 */
    if (End <= TOY_IDENTITY_BYTES) {
        if (OutVirt != 0) {
            *OutVirt = Phys;
        }
        return 0;
    }

    Cur = Phys & ~(HUGE_SIZE - 1ull);
    while (Cur < End) {
        if (MapHugeIdentityMmio(Cur) != 0) {
            return -1;
        }
        Mapped = 1;
        Cur += HUGE_SIZE;
    }
    if (Mapped) {
        TlbFlushAll();
    }
    if (OutVirt != 0) {
        *OutVirt = Phys;
    }
    return 0;
#else
    (void)Phys;
    (void)Size;
    if (OutVirt != 0) {
        *OutVirt = 0;
    }
    return -1;
#endif
}

int VirtualMemoryInitialize(void) {
    const BOOT_INFO *Info = BootInfoGet();

    gVmReady = 0;

#if defined(__x86_64__) || defined(_M_X64)
    if (!Cr0PagingOn()) {
        VmLog("VMM: CR0.PG off (EarlyIdentity missing?)\n");
        return -1;
    }
    {
        UINT64 Root = EarlyIdentityRoot();
        UINT64 Cr3 = ReadCr3();

        VmLog("VMM: PG on cr3=");
        VmLogHex64(Cr3);
        VmLog(" root=");
        VmLogHex64(Root);
        VmLog("\n");
        if (Root != 0 && (Cr3 & ~0xFFFull) != (Root & ~0xFFFull)) {
            VmLog("VMM: warn CR3!=EarlyRoot\n");
        }
    }
    if (Info != 0 && Info->FrameBufferBase != 0 && Info->FrameBufferSize != 0) {
        if (VirtualMemoryMapIdentity(Info->FrameBufferBase,
                                     Info->FrameBufferSize) != 0) {
            VmLog("VMM: FB outside identity window\n");
            return -1;
        }
        VmLog("VMM: FB in identity window\n");
    }
    /* K13：Boot 交出的高址 xHCI BAR 先映上，供 USB 读 CAP */
    if (Info != 0 && Info->XhciBase != 0) {
        UINT64 Virt = 0;

        if (VirtualMemoryMapMmio(Info->XhciBase, 0x1000ull, &Virt) != 0) {
            VmLog("VMM: map mmio FAIL @0x");
            VmLogHex64(Info->XhciBase);
            VmLog("\n");
            return -1;
        }
        VmLog("VMM: map mmio ok @0x");
        VmLogHex64(Virt);
        VmLog("\n");
    }
#else
    (void)Info;
    VmLog("VMM: stub ok (no MMU knife yet)\n");
#endif

    gVmReady = 1;
    return 0;
}

void VirtualMemoryEnable(void) {
    (void)gVmReady;
}
