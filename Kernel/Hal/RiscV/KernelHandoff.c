/*
 * KernelHandoff.c — RiscV：填写 BOOT_INFO，再调用 KernelMain（仅 BSP）
 *
 * 【初学者】
 * OpenSBI / QEMU `-kernel` 把控制权交给 KernelEntry；只有抢到 BSP 的
 * hart 会进入本函数（参数：hartid + DTB）。次核停在 SecondaryPark。
 *
 * virt 约定：RAM @ 0x80000000；内核载荷常在 0x80200000。
 * 填 BOOT_INFO → 串口横幅 → KernelMain。与 Arm64 同思路，地址不同。
 */
#include "BootInfoTypes.h"
#include "Kernel.h"
#include "HalSerial.h"
#include "BoardConfig.h"

extern char __kernel_end[];

#define RISCV_VIRT_RAM_BASE 0x80000000ULL
#define RISCV_VIRT_RAM_SIZE (256ULL * 1024ULL * 1024ULL)
#define RISCV_VIRT_KERNEL_LOAD 0x80200000ULL /* OpenSBI 默认载荷地址 */
#define FDT_MAGIC 0xd00dfeedu

static void MemZero(void *P, UINTN N) {
    UINT8 *B = (UINT8 *)P;
    UINTN i;
    for (i = 0; i < N; i++) {
        B[i] = 0;
    }
}

/* FDT 大端字段 → 主机序（riscv64 小端） */
static UINT32 Be32(const void *P) {
    const UINT8 *B = (const UINT8 *)P;
    return ((UINT32)B[0] << 24) | ((UINT32)B[1] << 16) |
           ((UINT32)B[2] << 8) | (UINT32)B[3];
}

/*
 * DtbMemoryRegion — 与 Arm64/Boot.c 同算法的极简 FDT 扫描
 *
 * 只找 memory / memory@* 的 reg，写出一段 (Base, Size)。
 * 逻辑注释见 Arm64 版；两边应保持行为一致，改一处记得改另一处。
 */
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
        if (Token == 1u) { /* BEGIN_NODE */
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

void KernelHandoff(UINT64 HartId, UINT64 DtbPhys) {
    (void)HartId;
    (void)DtbPhys;
    HalSerialInitialize();
    HalSerialWrite("board: ");
    HalSerialWrite(TOY_BOARD_NAME);
    HalSerialWrite(" (bringup)\n");
    for (;;) {
    }
}

#else

/*
 * KernelHandoff — 仅 BSP 调用（见 Boot.S）
 *
 * @param HartId   硬件 hart 号（当前组表不用，保留便于以后写进 Info）
 * @param DtbPhys  OpenSBI 传来的 DTB；无效则回退 RAM 常量且 DtbPhys=0
 *
 * 与 Arm64 差别：
 *   - KernelStart 默认 0x80200000（不是 RAM 基址 0x80000000）；
 *   - 无「第二处固定 DTB 地址」回试（只信 a1 或放弃）。
 */
void KernelHandoff(UINT64 HartId, UINT64 DtbPhys) {
    static BOOT_INFO Info;
    UINT64 KernelStart = RISCV_VIRT_KERNEL_LOAD;
    UINT64 KernelEnd = (UINT64)(UINTN)__kernel_end;
    UINT64 RamBase = RISCV_VIRT_RAM_BASE;
    UINT64 RamSize = RISCV_VIRT_RAM_SIZE;
    UINT64 FreeStart;
    int FromDtb;

    (void)HartId;
    MemZero(&Info, sizeof(Info));

    FromDtb = (DtbMemoryRegion(DtbPhys, &RamBase, &RamSize) == 0);
    if (!FromDtb) {
        RamBase = RISCV_VIRT_RAM_BASE;
        RamSize = RISCV_VIRT_RAM_SIZE;
    }

    Info.KernelStart = KernelStart;
    Info.KernelEnd = KernelEnd;
    Info.DtbPhys = FromDtb ? DtbPhys : 0;

    /* [RamBase, FreeStart) 保留；[FreeStart, RamEnd) 可分配 */
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

    HalSerialInitialize();
    HalSerialWrite("handoff: BOOT_INFO ready\n");
    KernelMain(&Info);
    for (;;) {
    }
}

#endif
