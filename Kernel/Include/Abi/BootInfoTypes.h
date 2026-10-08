/*
 * BootInfoTypes.h — BOOT_INFO / VIDEO_CONFIG 布局（ABI，无仓储）
 *
 * 【分层】
 *   Abi（本头）     — 结构体契约；Hal / Core 都可 include
 *   Core/BootInfo.c — BootInfoStore / Get（全局仓，仅 Core 与模块用）
 *   Hal/Handoff     — 填一份 BOOT_INFO，交给 KernelMain；不调用 Store/Get
 */
#ifndef BOOT_INFO_TYPES_H
#define BOOT_INFO_TYPES_H

#include "BootTypes.h"

#define BOOT_MEMORY_REGIONS_MAX 64
#define BOOT_VIDEO_MODE_MAX     32

typedef struct {
    UINT64 Phys;
    UINT64 Size;
    UINT32 Free;
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

    UINT64 DtbPhys;
    /* Boot PCI 扫到的 xHCI MMIO 基址；0 = 未找到 */
    UINT64 XhciBase;
    /* 1 = Boot（或后刀真探盘）见过系统卷标记 TOYOS.ID */
    UINT32 ToyOsIdSeen;
} BOOT_INFO;

typedef struct {
    UINT64 FrameBufferBase;
    UINT64 FrameBufferSize;
    UINT32 HorizontalResolution;
    UINT32 VerticalResolution;
    UINT32 PixelsPerScanLine;
} VIDEO_CONFIG;

#endif
