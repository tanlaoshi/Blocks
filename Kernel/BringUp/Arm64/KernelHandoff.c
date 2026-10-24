/*
 * KernelHandoff.c — Arm64：DTB → BOOT_HANDOFF → KernelMain（ramfb 可选）
 *
 * QEMU 对 ELF -kernel 不走 Linux 启动协议（x0 常为 0）。run-virt-arm.sh
 * 用 dumpdtb + -device loader 把 FDT 放到 ARM64_VIRT_DTB_ADDR。
 */
#include "HalSerial.h"
#include "Hal.h"
#include "BootHandoff.h"
#include "Kernel.h"
#include "Virtio/Dtb.h"
#include "Virtio/Ramfb.h"
#include "Board.h"
#include "BoardConfig.h"
#include "Text.h"

extern char __kernel_end[];

/* 与 run-virt-arm.sh 中 loader addr 一致（落在 ≥256MiB RAM 内）；非 virt 不用 */
#define ARM64_VIRT_DTB_ADDR 0x4a000000ULL
#define ARM64_VIRT_FWCFG_FALLBACK 0x09020000ULL

#if BRINGUP

void KernelHandoff(UINT64 DtbPhys) {
    (void)DtbPhys;
    HalSerialInitialize();
    HalSerialWrite("Boot: Arm64 Hello\n");
    HalSerialWrite("board: ");
    HalSerialWrite(BoardName());
    HalSerialWrite("\n");
    HalCpuHalt();
}

#else

static int BootHandoffAddRegion(BOOT_HANDOFF *Info, UINT64 Phys, UINT64 Size, int Free) {
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

static int TryDtb(UINT64 Phys, UINT64 *RamBase, UINT64 *RamSize) {
    if (Phys == 0) {
        return -1;
    }
    return DtbMemoryRegion(Phys, RamBase, RamSize);
}

void KernelHandoff(UINT64 DtbPhys) {
    BOOT_HANDOFF Info;
    UINT64 KernelStart = BOARD_KERNEL_LOAD;
    UINT64 KernelEnd = (UINT64)(UINTN)__kernel_end;
    UINT64 RamBase = BOARD_RAM_BASE;
    UINT64 RamSize = BOARD_RAM_SIZE;
    UINT64 FreeStart;
    UINT64 ReserveEnd;
    UINT64 UsedDtb = 0;
    UINT64 FwCfg = 0;
    UINTN i;
    int FromDtb;

    HalSerialInitialize();
    HalSerialWrite("Boot: Arm64 KernelMain\n");
    HalSerialWrite("board: ");
    HalSerialWrite(BoardName());
    HalSerialWrite("\n");
    {
        UINT64 El;
        __asm__ volatile("mrs %0, CurrentEL" : "=r"(El));
        HalSerialWrite("Boot: EL=");
        HexU64(El >> 2);
        HalSerialWrite("\n");
    }

    FromDtb = (TryDtb(DtbPhys, &RamBase, &RamSize) == 0);
    if (FromDtb) {
        UsedDtb = DtbPhys;
#if BOARD_IS_VIRT
    } else if (TryDtb(ARM64_VIRT_DTB_ADDR, &RamBase, &RamSize) == 0) {
        FromDtb = 1;
        UsedDtb = ARM64_VIRT_DTB_ADDR;
#endif
    }

    if (FromDtb) {
        HalSerialWrite("Boot: DTB @");
        HexU64(UsedDtb);
        HalSerialWrite(" memory base=");
        HexU64(RamBase);
        HalSerialWrite(" size=");
        HexU64(RamSize);
        HalSerialWrite("\n");
    } else {
        HalSerialWrite("Boot: DTB Memory Missing, Fallback BOARD_RAM\n");
        RamBase = BOARD_RAM_BASE;
        RamSize = BOARD_RAM_SIZE;
    }

    for (i = 0; i < sizeof(Info); i++) {
        ((UINT8 *)&Info)[i] = 0;
    }
    Info.KernelStart = KernelStart;
    Info.KernelEnd = KernelEnd;

    FreeStart = (KernelEnd + 0xFFFULL) & ~0xFFFULL;
    if (FreeStart < KernelStart) {
        FreeStart = KernelStart;
    }

#if BOARD_IS_VIRT
    if (UsedDtb != 0 && DtbFwCfgBase(UsedDtb, &FwCfg) != 0) {
        FwCfg = 0;
    }
    if (FwCfg == 0) {
        FwCfg = ARM64_VIRT_FWCFG_FALLBACK;
    }
    (void)RamfbSetup(&Info, FwCfg, &FreeStart, RamBase + RamSize);
#else
    (void)FwCfg;
#endif

    ReserveEnd = FreeStart;
    if (ReserveEnd < RamBase) {
        ReserveEnd = RamBase;
    }
    if (ReserveEnd > RamBase && ReserveEnd - RamBase > 0) {
        UINT64 ResSize = ReserveEnd - RamBase;
        if (ResSize > RamSize) {
            ResSize = RamSize;
        }
        BootHandoffAddRegion(&Info, RamBase, ResSize, 0);
    }
    if (FreeStart < RamBase + RamSize && FreeStart >= RamBase) {
        BootHandoffAddRegion(&Info, FreeStart, RamBase + RamSize - FreeStart, 1);
    } else if (Info.RegionCount == 0 && RamSize > 0) {
        BootHandoffAddRegion(&Info, RamBase, RamSize, 1);
    }

    BootHandoffSet(&Info);
#if BOARD_IS_VIRT
    HalSmpNoteDtb(UsedDtb ? UsedDtb : ARM64_VIRT_DTB_ADDR);
#else
    if (UsedDtb != 0) {
        HalSmpNoteDtb(UsedDtb);
    }
#endif
    KernelMain();
    for (;;) {
        HalCpuPark();
    }
}

#endif
