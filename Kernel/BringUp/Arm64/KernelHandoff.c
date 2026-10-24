/*
 * KernelHandoff.c — Arm64：DTB → BOOT_HANDOFF → KernelMain（ramfb / Pi mailbox FB）
 *
 * QEMU 对 ELF -kernel 不走 Linux 启动协议（x0 常为 0）。run-virt-arm.sh
 * 用 dumpdtb + -device loader 把 FDT 放到 ARM64_VIRT_DTB_ADDR。
 *
 * Pi：bootelf 的 x0≠DTB；FAT 上 pi.dtb memory 常为 <0 0> 模板。
 * 真机 FB：BoardFbSetup（mailbox）后尽早 HalSerialGopEnable，Boot: 上 HDMI。
 */
#include "HalSerial.h"
#include "Hal.h"
#include "HalVideo.h"
#include "BootHandoff.h"
#include "Kernel.h"
#include "Virtio/Dtb.h"
#include "Virtio/Ramfb.h"
#include "Board.h"
#include "BoardConfig.h"
#include "Text.h"
#include "Font.h"

extern char __kernel_end[];

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

static void BootHexU64(UINT64 V) {
    char Buf[17];

    FormatHexadecimal(Buf, (int)sizeof(Buf), V, 16, 0, 0);
    HalSerialBootMark(Buf);
}

static int TryDtb(UINT64 Phys, UINT64 *RamBase, UINT64 *RamSize) {
    if (Phys == 0) {
        return -1;
    }
    return DtbMemoryRegion(Phys, RamBase, RamSize);
}

static void PrintRegions(const BOOT_HANDOFF *Info) {
    UINT32 i;

    HalSerialBootMark("Boot: regions=");
    BootHexU64(Info->RegionCount);
    HalSerialBootMark("\n");
    for (i = 0; i < Info->RegionCount; i++) {
        HalSerialBootMark("Boot: region[");
        BootHexU64(i);
        HalSerialBootMark("] phys=");
        BootHexU64(Info->Regions[i].Phys);
        HalSerialBootMark(" size=");
        BootHexU64(Info->Regions[i].Size);
        HalSerialBootMark(Info->Regions[i].Free ? " free\n" : " reserved\n");
    }
}

/* FB 已填进 Info 后：挂 Video + 屏上滚，后续 BootMark 可见 */
static void EarlyGopFromHandoff(const BOOT_HANDOFF *Info) {
    VIDEO_CONFIG Video;

    if (Info == 0 || Info->FrameBufferSize == 0) {
        return;
    }
    BootHandoffSet(Info);
    Video = BootHandoffToVideoConfig(Info);
    FontInitialize();
    HalVideoSet(&Video);
    HalSerialGopEnable();
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
    int FromMbox;

    HalSerialInitialize();
    HalSerialBootMark("Boot: Arm64 KernelMain\n");
    HalSerialBootMark("board: ");
    HalSerialBootMark(BoardName());
    HalSerialBootMark("\n");
    {
        UINT64 El;
        __asm__ volatile("mrs %0, CurrentEL" : "=r"(El));
        HalSerialBootMark("Boot: EL=");
        BootHexU64(El >> 2);
        HalSerialBootMark("\n");
    }

    FromDtb = 0;
    FromMbox = 0;
    if (TryDtb(DtbPhys, &RamBase, &RamSize) == 0) {
        FromDtb = 1;
        UsedDtb = DtbPhys;
    } else if (TryDtb(BOARD_DTB_LOAD, &RamBase, &RamSize) == 0) {
        FromDtb = 1;
        UsedDtb = BOARD_DTB_LOAD;
#if BOARD_IS_VIRT
    } else if (TryDtb(ARM64_VIRT_DTB_ADDR, &RamBase, &RamSize) == 0) {
        FromDtb = 1;
        UsedDtb = ARM64_VIRT_DTB_ADDR;
#endif
    } else if (BoardDramProbe(&RamBase, &RamSize) == 0) {
        FromMbox = 1;
    } else {
        HalSerialBootMark("Boot: DTB/mbox Memory Missing, Fallback BOARD_RAM\n");
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
    EarlyGopFromHandoff(&Info);
#else
    (void)FwCfg;
    if (BoardFbSetup(&Info) == 0) {
        EarlyGopFromHandoff(&Info);
        /* FB dcache 之后再拉一次 UART，确认未被 civac 扫坏 */
        HalSerialInitialize();
        HalSerialBootMark("Boot: uart GPFSEL1=");
        BootHexU64(*(volatile UINT32 *)(UINTN)0x3F200004ULL);
        HalSerialBootMark(" MU_CNTL=");
        BootHexU64(*(volatile UINT32 *)(UINTN)0x3F215060ULL);
        HalSerialBootMark("\n");
        HalSerialBootMark("Boot: mbox FB base=");
        BootHexU64(Info.FrameBufferBase);
        HalSerialBootMark(" size=");
        BootHexU64(Info.FrameBufferSize);
        HalSerialBootMark(" ");
        BootHexU64(Info.HorizontalResolution);
        HalSerialBootMark("x");
        BootHexU64(Info.VerticalResolution);
        HalSerialBootMark("\n");
    } else {
        HalSerialBootMark("Boot: mbox FB failed (serial only)\n");
    }
#endif

    if (FromDtb) {
        HalSerialBootMark("Boot: DTB @");
        BootHexU64(UsedDtb);
        HalSerialBootMark(" memory base=");
        BootHexU64(RamBase);
        HalSerialBootMark(" size=");
        BootHexU64(RamSize);
        HalSerialBootMark("\n");
    } else if (FromMbox) {
        HalSerialBootMark("Boot: mbox GET_ARM_MEMORY base=");
        BootHexU64(RamBase);
        HalSerialBootMark(" size=");
        BootHexU64(RamSize);
        HalSerialBootMark("\n");
    }

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

    PrintRegions(&Info);
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
