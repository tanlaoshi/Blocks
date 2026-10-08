/*
 * Boot.c — Arm64：组 BOOT_INFO → KernelMain（只依赖本目录 BootInfo.h）
 *
 * QEMU virt：x0/DTB 常需 loader；失败则回退 256MiB @0x40000000。
 */
#include "BootInfo.h"

extern char __kernel_end[];

#define ARM64_VIRT_DTB_ADDR 0x4a000000ULL
#define ARM64_VIRT_RAM_BASE 0x40000000ULL
#define ARM64_VIRT_RAM_SIZE (256ULL * 1024ULL * 1024ULL)
#define FDT_MAGIC 0xd00dfeedu

static void MemZero(void *P, UINTN N) {
    UINT8 *B = (UINT8 *)P;
    UINTN i;
    for (i = 0; i < N; i++) {
        B[i] = 0;
    }
}

static UINT32 Be32(const void *P) {
    const UINT8 *B = (const UINT8 *)P;
    return ((UINT32)B[0] << 24) | ((UINT32)B[1] << 16) |
           ((UINT32)B[2] << 8) | (UINT32)B[3];
}

/* 极简：只认根下 memory@ 的 reg（boot 自用，不引 Kernel/Dtb.h） */
static int DtbMemoryRegion(UINT64 DtbPhys, UINT64 *OutBase, UINT64 *OutSize) {
    const UINT8 *Blob;
    UINT32 Total, StructOff, StructSize, StringsOff, Off, End;
    UINT32 AddrCells = 2, SizeCells = 1;
    int HaveMem = 0;
    UINT64 MemBase = 0, MemSize = 0;

    if (!OutBase || !OutSize || DtbPhys == 0) {
        return -1;
    }
    Blob = (const UINT8 *)(UINTN)DtbPhys;
    if (Be32(Blob) != FDT_MAGIC) {
        return -1;
    }
    Total = Be32(Blob + 4);
    StructOff = Be32(Blob + 8);
    StructSize = Be32(Blob + 36);
    StringsOff = Be32(Blob + 12);
    if (Total < 40 || StructOff >= Total || StringsOff >= Total ||
        StructSize == 0 || StructOff + StructSize > Total) {
        return -1;
    }
    Off = StructOff;
    End = StructOff + StructSize;
    while (Off + 4 <= End) {
        UINT32 Token = Be32(Blob + Off);
        Off += 4;
        if (Token == 1u) { /* FDT_BEGIN_NODE */
            const char *Name = (const char *)(Blob + Off);
            UINT32 Len = 0;
            int IsMem;
            while (Off + Len < End && Name[Len]) {
                Len++;
            }
            Off = (Off + Len + 1 + 3u) & ~3u;
            IsMem = (Name[0] == 'm' && Name[1] == 'e' && Name[2] == 'm' &&
                     Name[3] == 'o' && Name[4] == 'r' && Name[5] == 'y' &&
                     (Name[6] == 0 || Name[6] == '@'));
            if (!IsMem) {
                continue;
            }
            /* 扫此节点内 prop 直到 END_NODE；简化：找 reg */
            while (Off + 4 <= End) {
                UINT32 T2 = Be32(Blob + Off);
                Off += 4;
                if (T2 == 2u) { /* END_NODE */
                    break;
                }
                if (T2 == 3u) { /* PROP */
                    UINT32 PLen = Be32(Blob + Off);
                    UINT32 PName = Be32(Blob + Off + 4);
                    const char *Pstr = (const char *)(Blob + StringsOff + PName);
                    const UINT8 *Val = Blob + Off + 8;
                    Off = (Off + 8 + PLen + 3u) & ~3u;
                    if (Pstr[0] == '#' && Pstr[1] == 'a') {
                        AddrCells = Be32(Val);
                    } else if (Pstr[0] == '#' && Pstr[1] == 's') {
                        SizeCells = Be32(Val);
                    } else if (Pstr[0] == 'r' && Pstr[1] == 'e' && Pstr[2] == 'g' &&
                               Pstr[3] == 0) {
                        UINT32 Need = (AddrCells + SizeCells) * 4u;
                        UINT32 i;
                        if (PLen < Need) {
                            continue;
                        }
                        MemBase = 0;
                        for (i = 0; i < AddrCells; i++) {
                            MemBase = (MemBase << 32) | Be32(Val + i * 4);
                        }
                        MemSize = 0;
                        for (i = 0; i < SizeCells; i++) {
                            MemSize = (MemSize << 32) | Be32(Val + (AddrCells + i) * 4);
                        }
                        HaveMem = 1;
                    }
                    continue;
                }
                if (T2 == 4u) { /* NOP */
                    continue;
                }
                if (T2 == 9u) { /* END */
                    break;
                }
                return -1;
            }
            continue;
        }
        if (Token == 2u || Token == 4u) {
            continue;
        }
        if (Token == 9u) {
            break;
        }
        if (Token == 3u) {
            UINT32 PLen = Be32(Blob + Off);
            Off = (Off + 8 + PLen + 3u) & ~3u;
            continue;
        }
        return -1;
    }
    if (!HaveMem || MemSize == 0) {
        return -1;
    }
    *OutBase = MemBase;
    *OutSize = MemSize;
    return 0;
}

static int BootInfoAddRegion(BOOT_INFO *Info, UINT64 Phys, UINT64 Size, int Free) {
    if (Info->RegionCount >= BOOT_MEMORY_REGIONS_MAX || Size == 0) {
        return -1;
    }
    Info->Regions[Info->RegionCount].Phys = Phys;
    Info->Regions[Info->RegionCount].Size = Size;
    Info->Regions[Info->RegionCount].Free = Free ? 1u : 0u;
    Info->RegionCount++;
    return 0;
}

#if TOY_BRINGUP

void BootMain(UINT64 DtbPhys) {
    (void)DtbPhys;
    for (;;) {
    }
}

#else

void BootMain(UINT64 DtbPhys) {
    static BOOT_INFO Info;
    UINT64 KernelStart = ARM64_VIRT_RAM_BASE;
    UINT64 KernelEnd = (UINT64)(UINTN)__kernel_end;
    UINT64 RamBase = ARM64_VIRT_RAM_BASE;
    UINT64 RamSize = ARM64_VIRT_RAM_SIZE;
    UINT64 FreeStart;
    UINT64 UsedDtb = 0;
    int FromDtb;

    MemZero(&Info, sizeof(Info));

    FromDtb = (DtbMemoryRegion(DtbPhys, &RamBase, &RamSize) == 0);
    if (FromDtb) {
        UsedDtb = DtbPhys;
    } else if (DtbMemoryRegion(ARM64_VIRT_DTB_ADDR, &RamBase, &RamSize) == 0) {
        FromDtb = 1;
        UsedDtb = ARM64_VIRT_DTB_ADDR;
    }
    if (!FromDtb) {
        RamBase = ARM64_VIRT_RAM_BASE;
        RamSize = ARM64_VIRT_RAM_SIZE;
    }

    Info.KernelStart = KernelStart;
    Info.KernelEnd = KernelEnd;
    Info.DtbPhys = UsedDtb;

    FreeStart = (KernelEnd + 0xFFFULL) & ~0xFFFULL;
    if (FreeStart < KernelStart) {
        FreeStart = KernelStart;
    }
    if (FreeStart > RamBase && FreeStart - RamBase <= RamSize) {
        BootInfoAddRegion(&Info, RamBase, FreeStart - RamBase, 0);
        if (FreeStart < RamBase + RamSize) {
            BootInfoAddRegion(&Info, FreeStart, RamBase + RamSize - FreeStart, 1);
        }
    } else if (RamSize > 0) {
        BootInfoAddRegion(&Info, RamBase, RamSize, 1);
    }

    KernelMain(&Info);
    for (;;) {
    }
}

#endif
