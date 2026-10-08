/*
 * BootInfo.h — Arm64 Boot → Kernel 的交接清单（本架构副本）
 *
 * 流程位置：
 *   Boot.S:KernelEntry → Boot.c:BootMain 填好本结构 → KernelMain(Info)
 *
 * 三份必须字节级一致（改布局请同步）：
 *   1) Boot/Arm64/BootInfo.h   （本文件）
 *   2) Boot/RiscV/BootInfo.h
 *   3) Kernel/Include/Core/BootInfo.h
 *
 * X64 路径不同：UEFI Boot 先交 BOOT_CONFIG，由 HAL Startup 转成同一份 BOOT_INFO。
 * Arm64/RiscV 没有那一步——Boot 直接组好再进 KernelMain。
 *
 * 本目录 Boot.c 只应 #include "BootInfo.h"（已含 BootTypes.h）。
 */
#ifndef BOOT_INFO_H
#define BOOT_INFO_H

#include "BootTypes.h"

#define BOOT_MEMORY_REGIONS_MAX 64 /* 内存段表上限；超出则丢弃后续段 */
#define BOOT_VIDEO_MODE_MAX     32 /* 可选分辨率列表上限（virt 早期常为 0） */

/* 一段物理内存：Free=1 可供 PMM 分配，Free=0 保留（内核/DTB/空洞等） */
typedef struct {
    UINT64 Phys;
    UINT64 Size;
    UINT32 Free;
} BOOT_MEMORY_REGION;

/* Settings 等用的「可选模式」条目；Arm virt 开机时常 VideoModeCount=0 */
typedef struct {
    UINT32 Width;
    UINT32 Height;
    UINT32 ModeNumber;
} BOOT_VIDEO_MODE;

/*
 * BOOT_INFO — 三种架构汇合后给 KernelMain 的标准清单
 *
 * 帧缓冲字段：QEMU virt 早期可能全 0（串口-only 或稍后由 HAL 开 ramfb）。
 * Regions[]：BootMain 根据 DTB/回退表切出「内核占用」与「可分配」两段。
 * DtbPhys：设备树物理地址；0 = 未拿到（Kernel 侧勿解引用）。
 */
typedef struct {
    UINT64 FrameBufferBase;
    UINT64 FrameBufferSize;
    UINT32 HorizontalResolution;
    UINT32 VerticalResolution;
    UINT32 PixelsPerScanLine;

    BOOT_MEMORY_REGION Regions[BOOT_MEMORY_REGIONS_MAX];
    UINT32             RegionCount;

    UINT64 KernelStart; /* 内核镜像大致起点（virt 常为 RAM 基址） */
    UINT64 KernelEnd;   /* 链接脚本符号 __kernel_end（映像末尾） */

    UINT32          VideoModeCount;
    BOOT_VIDEO_MODE VideoModes[BOOT_VIDEO_MODE_MAX];
    UINT64          GopProtocol; /* X64 UEFI 用；Arm/RiscV 填 0 */

    UINT64          DtbPhys; /* 0 = 无 DTB */
} BOOT_INFO;

/* Common 大门：约定进入时 Info 已填好（或调用方保证非 NULL） */
void KernelMain(const BOOT_INFO *Info);

#endif
