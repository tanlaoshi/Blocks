/*
 * BootInfo.h — 内核侧「开机说明书」BOOT_INFO
 *
 * 【初学者 · 为什么需要它？】
 * 固件形态五花八门：
 *   - X64：UEFI 给一份很「UEFI 味」的 UEFI_BOOT_CONFIG（内存图、GOP…）
 *   - Arm64/RiscV virt：往往只有 DTB + 约定 RAM
 * 内核 Common（调度、FS、桌面）不应该懂这些差异。
 * 于是各 Arch 的 KernelHandoff 负责翻译成同一份 BOOT_INFO，
 * 再调用 KernelMain(Info)。从这里起，三架构合流。
 *
 * 【谁写 / 谁读】
 *   写：Hal/<Arch>/KernelHandoff.c → BootInfoSet()
 *   读：KernelMain、Memory、Video… 经 BootInfoGet()
 *
 * 【积木】胶水契约（ABI）。字段可加，但改布局要三架构一起改。
 */
#ifndef BOOT_INFO_H
#define BOOT_INFO_H

#include "BootTypes.h"

#define BOOT_MEMORY_REGIONS_MAX 64
#define BOOT_VIDEO_MODE_MAX     32

/* 一段物理内存：Free=1 表示日后 PMM 可拿来分配 */
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
    /* 帧缓冲（没有屏则 Size=0） */
    UINT64 FrameBufferBase;
    UINT64 FrameBufferSize;
    UINT32 HorizontalResolution;
    UINT32 VerticalResolution;
    UINT32 PixelsPerScanLine;

    /* 物理内存清单（给 PMM 涂色用） */
    BOOT_MEMORY_REGION Regions[BOOT_MEMORY_REGIONS_MAX];
    UINT32             RegionCount;

    /* 内核映像在物理内存中的大致范围 */
    UINT64 KernelStart;
    UINT64 KernelEnd;

    /* 可选：Boot 枚举的显示模式 / GOP 指针（X64） */
    UINT32          VideoModeCount;
    BOOT_VIDEO_MODE VideoModes[BOOT_VIDEO_MODE_MAX];
    UINT64          GopProtocol;

    /* Arm/RiscV：设备树物理址；X64 为 0 */
    UINT64          DtbPhys;
} BOOT_INFO;

/* 从 BOOT_INFO 抽出的「当前模式」视频配置，给 HalVideoSet 用 */
typedef struct {
    UINT64 FrameBufferBase;
    UINT64 FrameBufferSize;
    UINT32 HorizontalResolution;
    UINT32 VerticalResolution;
    UINT32 PixelsPerScanLine;
} VIDEO_CONFIG;

/* Common 大门：参数是已填好的开机说明书 */
void KernelMain(const BOOT_INFO *Info);

void BootInfoSet(const BOOT_INFO *Info);
const BOOT_INFO *BootInfoGet(void);
VIDEO_CONFIG BootInfoToVideoConfig(const BOOT_INFO *Info);

#endif
