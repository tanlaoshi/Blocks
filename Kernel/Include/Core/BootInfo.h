/*
 * BootInfo.h — Kernel 侧 BOOT_INFO + Set/Get
 *
 * 结构布局须与 Boot/Arm64/BootInfo.h、Boot/RiscV/BootInfo.h 保持一致。
 * X64：HAL 从 TOY_BOOT_CONFIG 转成此结构后再进 KernelMain。
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
    UINT64          GopProtocol;

    UINT64          DtbPhys; /* 0 = 无 */
} BOOT_INFO;

typedef struct {
    UINT64 FrameBufferBase;
    UINT64 FrameBufferSize;
    UINT32 HorizontalResolution;
    UINT32 VerticalResolution;
    UINT32 PixelsPerScanLine;
} VIDEO_CONFIG;

void KernelMain(const BOOT_INFO *Info);

void BootInfoSet(const BOOT_INFO *Info);
const BOOT_INFO *BootInfoGet(void);
VIDEO_CONFIG BootInfoToVideoConfig(const BOOT_INFO *Info);

#endif
