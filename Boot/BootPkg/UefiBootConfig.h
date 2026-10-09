/*
 * UefiBootConfig.h — UEFI Boot → HAL 的交接包（UEFI_BOOT_CONFIG）
 *
 * 命名按固件形态（UEFI），不绑死 CPU：现用于 Boot；若以后有
 * AArch64 UEFI Boot，可共用本布局（与 QEMU virt -kernel 的 Arm/RiscV 无关）。
 *
 * 权威在本目录；Kernel/Hal 侧经 BootConfig.h 引用。
 * 布局 632 字节；改字段须两边一起改并更新 _Static_assert。
 *
 * VideoMode 带 ModeNumber；末尾 GopProtocol 供真机热切。
 */
#ifndef UEFI_BOOT_CONFIG_H
#define UEFI_BOOT_CONFIG_H

#ifndef EFIAPI
#include "BootTypes.h"
typedef void VOID;
#endif

#define UEFI_VIDEO_MODE_MAX 32
#define UEFI_GOP_HANDOFF_MAGIC 0x314E4F47u /* 'GON1' */

typedef struct {
    UINT64 FrameBufferBase;
    UINT64 FrameBufferSize;
    UINT32 HorizontalResolution;
    UINT32 VerticalResolution;
    UINT32 PixelsPerScanLine;
} UEFI_VIDEO_CONFIG;

typedef struct {
    UINT32 Width;
    UINT32 Height;
    UINT32 ModeNumber;
    UINT32 Reserved;
} UEFI_VIDEO_MODE;

typedef struct {
    VOID  *Buffer;
    UINTN  MapSize;
    UINTN  MapKey;
    UINTN  DescriptorSize;
    UINT32 DescriptorVersion;
} UEFI_MEMORY_MAP;

typedef struct {
    UEFI_VIDEO_CONFIG     VideoConfig;
    UEFI_MEMORY_MAP       MemoryMap;
    UINT64               EntryAddress; /* 内核镜像入口地址（数字，不是函数名） */
    UINT64               RsdpAddress;
    VOID                *SystemTable;
    UINT64               XhciBaseAddress;
    /*
     * ExitBootServices 后内核无法 QueryMode。
     * Boot 枚举可用 GOP 模式供 Settings 列表（去重 WxH）。
     * ModeNumber + GopProtocol 供真机运行时 SetMode。
     */
    UINT32               VideoModeCount;
    UINT32               VideoModePad; /* GOP handoff：UEFI_GOP_HANDOFF_MAGIC */
    UEFI_VIDEO_MODE       VideoModes[UEFI_VIDEO_MODE_MAX];
    UINT64               GopProtocol;
    /* K9：Boot 见过 BLOCKS.ID 则为 1 */
    UINT32               OsIdSeen;
    UINT32               OsIdPad;
} UEFI_BOOT_CONFIG;

#if defined(__GNUC__)
_Static_assert(sizeof(UEFI_VIDEO_CONFIG) == 32, "UEFI_VIDEO_CONFIG size");
_Static_assert(sizeof(UEFI_MEMORY_MAP) == 40, "UEFI_MEMORY_MAP size");
_Static_assert(sizeof(UEFI_VIDEO_MODE) == 16, "UEFI_VIDEO_MODE size");
_Static_assert(sizeof(UEFI_BOOT_CONFIG) == 640, "UEFI_BOOT_CONFIG size");
_Static_assert(__builtin_offsetof(UEFI_BOOT_CONFIG, VideoConfig) == 0, "VideoConfig off");
_Static_assert(__builtin_offsetof(UEFI_BOOT_CONFIG, MemoryMap) == 32, "MemoryMap off");
_Static_assert(__builtin_offsetof(UEFI_BOOT_CONFIG, EntryAddress) == 72, "EntryAddress off");
_Static_assert(__builtin_offsetof(UEFI_BOOT_CONFIG, RsdpAddress) == 80, "RsdpAddress off");
_Static_assert(__builtin_offsetof(UEFI_BOOT_CONFIG, SystemTable) == 88, "SystemTable off");
_Static_assert(__builtin_offsetof(UEFI_BOOT_CONFIG, XhciBaseAddress) == 96, "XhciBase off");
_Static_assert(__builtin_offsetof(UEFI_BOOT_CONFIG, VideoModeCount) == 104, "VideoModeCount off");
_Static_assert(__builtin_offsetof(UEFI_BOOT_CONFIG, VideoModes) == 112, "VideoModes off");
_Static_assert(__builtin_offsetof(UEFI_BOOT_CONFIG, GopProtocol) == 624, "GopProtocol off");
_Static_assert(__builtin_offsetof(UEFI_BOOT_CONFIG, OsIdSeen) == 632, "OsIdSeen off");
#endif

#endif
