/*
 * BootHandoff.h — 仅 X64 Boot → HAL 的交接包（Arm/RiscV 无此结构）
 *
 * 权威在本目录；内核侧迁入后须镜像同步。
 * 布局 632 字节；改字段须两边一起改并更新 _Static_assert。
 *
 * PR-G-hotres-pc：VideoMode 带 ModeNumber；末尾 GopProtocol 供真机热切。
 */
#ifndef X64_BOOT_HANDOFF_H
#define X64_BOOT_HANDOFF_H

#ifndef EFIAPI
#include "BootTypes.h"
typedef void VOID;
#endif

#define TOY_VIDEO_MODE_MAX 32
#define TOY_BOOT_GOP_HANDOFF_MAGIC 0x314E4F47u /* 'GON1' */

typedef struct {
    UINT64 FrameBufferBase;
    UINT64 FrameBufferSize;
    UINT32 HorizontalResolution;
    UINT32 VerticalResolution;
    UINT32 PixelsPerScanLine;
} TOY_VIDEO_CONFIG;

typedef struct {
    UINT32 Width;
    UINT32 Height;
    UINT32 ModeNumber;
    UINT32 Reserved;
} TOY_VIDEO_MODE;

typedef struct {
    VOID  *Buffer;
    UINTN  MapSize;
    UINTN  MapKey;
    UINTN  DescriptorSize;
    UINT32 DescriptorVersion;
} TOY_MEMORY_MAP;

typedef struct {
    TOY_VIDEO_CONFIG     VideoConfig;
    TOY_MEMORY_MAP       MemoryMap;
    UINT64               EntryAddress; /* 内核镜像入口地址（数字，不是函数名） */
    UINT64               RsdpAddress;
    VOID                *SystemTable;
    UINT64               XhciBaseAddress;
    /*
     * PR-G-modes：ExitBootServices 后内核无法 QueryMode。
     * Boot 枚举可用 GOP 模式供 Settings 列表（去重 WxH）。
     * PR-G-hotres-pc：ModeNumber + GopProtocol 供真机运行时 SetMode。
     */
    UINT32               VideoModeCount;
    UINT32               VideoModePad;
    TOY_VIDEO_MODE       VideoModes[TOY_VIDEO_MODE_MAX];
    UINT64               GopProtocol;
} X64_BOOT_CONFIG;

#if defined(__GNUC__)
_Static_assert(sizeof(TOY_VIDEO_CONFIG) == 32, "TOY_VIDEO_CONFIG size");
_Static_assert(sizeof(TOY_MEMORY_MAP) == 40, "TOY_MEMORY_MAP size");
_Static_assert(sizeof(TOY_VIDEO_MODE) == 16, "TOY_VIDEO_MODE size");
_Static_assert(sizeof(X64_BOOT_CONFIG) == 632, "X64_BOOT_CONFIG size");
_Static_assert(__builtin_offsetof(X64_BOOT_CONFIG, VideoConfig) == 0, "VideoConfig off");
_Static_assert(__builtin_offsetof(X64_BOOT_CONFIG, MemoryMap) == 32, "MemoryMap off");
_Static_assert(__builtin_offsetof(X64_BOOT_CONFIG, EntryAddress) == 72, "EntryAddress off");
_Static_assert(__builtin_offsetof(X64_BOOT_CONFIG, RsdpAddress) == 80, "RsdpAddress off");
_Static_assert(__builtin_offsetof(X64_BOOT_CONFIG, SystemTable) == 88, "SystemTable off");
_Static_assert(__builtin_offsetof(X64_BOOT_CONFIG, XhciBaseAddress) == 96, "XhciBase off");
_Static_assert(__builtin_offsetof(X64_BOOT_CONFIG, VideoModeCount) == 104, "VideoModeCount off");
_Static_assert(__builtin_offsetof(X64_BOOT_CONFIG, VideoModes) == 112, "VideoModes off");
_Static_assert(__builtin_offsetof(X64_BOOT_CONFIG, GopProtocol) == 624, "GopProtocol off");
#endif

#endif
