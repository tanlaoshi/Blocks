/*
 * BootHandoff.h — 各架构 handoff 填好后交给 Common 的交接块（原 BOOT_INFO）
 */
#ifndef BOOT_HANDOFF_H
#define BOOT_HANDOFF_H

#include "BootTypes.h"

#define BOOT_MEMORY_REGIONS_MAX 64
#define BOOT_VIDEO_MODE_MAX     32

typedef struct {
    UINT64 Phys;
    UINT64 Size;
    UINT32 Free; /* 1 = 可分配（Conventional），0 = 保留 */
} BOOT_MEMORY_REGION;

typedef struct {
    UINT32 Width;
    UINT32 Height;
    UINT32 ModeNumber; /* PR-G-hotres-pc：GOP SetMode 下标；0 也可为合法模式 */
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

    /* PR-G-modes：Boot 传入的可用显示模式（0=未知，Settings 回退硬编码表） */
    UINT32          VideoModeCount;
    BOOT_VIDEO_MODE VideoModes[BOOT_VIDEO_MODE_MAX];
    /* PR-G-hotres-pc：EFI GOP*；0=无交接 / 旧 Boot */
    UINT64          GopProtocol;
    /* 加载 BOOTX64.EFI 的 GPT Disk GUID；BootDiskGuidOk=0 表示旧 Boot */
    UINT8           BootDiskGuid[16];
    UINT32          BootDiskGuidOk;
} BOOT_HANDOFF;

typedef struct {
    UINT64 FrameBufferBase;
    UINT64 FrameBufferSize;
    UINT32 HorizontalResolution;
    UINT32 VerticalResolution;
    UINT32 PixelsPerScanLine;
} VIDEO_CONFIG;

void BootHandoffSet(const BOOT_HANDOFF *Handoff);
const BOOT_HANDOFF *BootHandoffGet(void);

VIDEO_CONFIG BootHandoffToVideoConfig(const BOOT_HANDOFF *Handoff);

#endif
