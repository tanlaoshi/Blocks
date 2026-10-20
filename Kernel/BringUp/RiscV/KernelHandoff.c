/*
 * KernelHandoff.c — RiscV：DTB / 板级表 → BOOT_INFO → KernelMain
 *
 * OpenSBI / 厂商 U-Boot：a0=hartid，a1=DTB。
 */
#include "HalSerial.h"
#include "Hal.h"
#include "BootInfo.h"
#include "Kernel.h"
#include "Virtio/Dtb.h"
#include "Virtio/Ramfb.h"
#include "Board.h"
#include "Text.h"

extern char __kernel_end[];

#define RISCV_VIRT_FWCFG_FALLBACK 0x10100000ULL

#if BRINGUP

void KernelHandoff(UINT64 HartId, UINT64 DtbPhys) {
    (void)HartId;
    (void)DtbPhys;
    HalSerialInitialize();
    HalSerialWrite("Boot: RiscV Hello\n");
    HalSerialWrite("board: ");
    HalSerialWrite(BoardName());
    HalSerialWrite("\n");
    HalCpuHalt();
}

#else

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

static void HexU64(UINT64 V) {
    char Buf[17];

    FormatHexadecimal(Buf, (int)sizeof(Buf), V, 16, 0, 0);
    HalSerialWrite(Buf);
}

void KernelHandoff(UINT64 HartId, UINT64 DtbPhys) {
    BOOT_INFO Info;
    UINT64 KernelStart = BOARD_KERNEL_LOAD;
    UINT64 KernelEnd = (UINT64)(UINTN)__kernel_end;
    UINT64 FirmwareEnd = KernelStart;
    UINT64 RamBase = BOARD_RAM_BASE;
    UINT64 RamSize = BOARD_RAM_SIZE;
    UINT64 FreeStart;
    UINT64 FwCfg = 0;
    UINTN i;
    int FromDtb;

    (void)HartId;

    HalSerialInitialize();
    HalSerialWrite("Boot: RiscV KernelMain\n");
    HalSerialWrite("board: ");
    HalSerialWrite(BoardName());
    HalSerialWrite("\n");

    FromDtb = (DtbMemoryRegion(DtbPhys, &RamBase, &RamSize) == 0);
    if (FromDtb) {
        HalSerialWrite("Boot: DTB Memory Base=");
        HexU64(RamBase);
        HalSerialWrite(" size=");
        HexU64(RamSize);
        HalSerialWrite("\n");
    } else {
        HalSerialWrite("Boot: DTB Memory Missing, Fallback BoardConfig RAM\n");
        RamBase = BOARD_RAM_BASE;
        RamSize = BOARD_RAM_SIZE;
    }

    for (i = 0; i < sizeof(Info); i++) {
        ((UINT8 *)&Info)[i] = 0;
    }
    Info.KernelStart = KernelStart;
    Info.KernelEnd = KernelEnd;

    FreeStart = (KernelEnd + 0xFFFULL) & ~0xFFFULL;
    if (FreeStart < FirmwareEnd) {
        FreeStart = FirmwareEnd;
    }

#if BOARD_HAS_FRAMEBUFFER
    if (FromDtb && DtbPhys != 0 && DtbFwCfgBase(DtbPhys, &FwCfg) != 0) {
        FwCfg = 0;
    }
    if (FwCfg == 0) {
        FwCfg = RISCV_VIRT_FWCFG_FALLBACK;
    }
    (void)RamfbSetup(&Info, FwCfg, &FreeStart, RamBase + RamSize);
#else
    (void)FwCfg;
#endif

    /* [RamBase, FreeStart) = 固件 + 内核（+ 可选 FB）；其后可分配 */
    if (FreeStart > RamBase && FreeStart - RamBase <= RamSize) {
        BootInfoAddRegion(&Info, RamBase, FreeStart - RamBase, 0);
        if (FreeStart < RamBase + RamSize) {
            BootInfoAddRegion(&Info, FreeStart, RamBase + RamSize - FreeStart, 1);
        }
    } else if (RamSize > 0) {
        BootInfoAddRegion(&Info, RamBase, RamSize, 1);
    }

    BootInfoSet(&Info);
    HalSmpNoteDtb(DtbPhys);
    KernelMain();
    for (;;) {
        HalCpuPark();
    }
}

#endif
