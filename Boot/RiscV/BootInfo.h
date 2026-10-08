/*
 * BootInfo.h — RiscV Boot → Kernel 的交接清单（本架构副本）
 *
 * 流程：Boot.S（BSP）→ BootMain(HartId, DtbPhys) 填表 → KernelMain(Info)
 *
 * 布局必须与下列文件一致：
 *   Boot/Arm64/BootInfo.h
 *   Kernel/Include/Core/BootInfo.h
 *
 * 次 hart 不填 BOOT_INFO：它们在 SecondaryPark 等到 SMP 信号后进 HalApMain。
 */
#ifndef BOOT_INFO_H
#define BOOT_INFO_H

#include "BootTypes.h"

#define BOOT_MEMORY_REGIONS_MAX 64
#define BOOT_VIDEO_MODE_MAX     32

typedef struct {
    UINT64 Phys;
    UINT64 Size;
    UINT32 Free; /* 1 = 可分配，0 = 保留 */
} BOOT_MEMORY_REGION;

typedef struct {
    UINT32 Width;
    UINT32 Height;
    UINT32 ModeNumber;
} BOOT_VIDEO_MODE;

typedef struct {
    UINT64 FrameBufferBase;
    UINT64 FrameBufferSize;
    UINT32 HorizontalResolution;
    UINT32 VerticalResolution;
    UINT32 PixelsPerScanLine;

    BOOT_MEMORY_REGION Regions[BOOT_MEMORY_REGIONS_MAX];
    UINT32             RegionCount;

    UINT64 KernelStart;
    UINT64 KernelEnd;

    UINT32          VideoModeCount;
    BOOT_VIDEO_MODE VideoModes[BOOT_VIDEO_MODE_MAX];
    UINT64          GopProtocol; /* 仅 X64 UEFI；此处为 0 */

    UINT64          DtbPhys; /* 0 = 无 */
} BOOT_INFO;

void KernelMain(const BOOT_INFO *Info);

#endif
