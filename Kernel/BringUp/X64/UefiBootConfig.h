/*
 * UefiBootConfig.h — 交接包的两份副本，内容必须相同（UEFI_BOOT_CONFIG，656 字节）
 *
 *   Boot/BootPkg/UefiBootConfig.h
 *   Kernel/BringUp/X64/UefiBootConfig.h
 *
 * 两边互不 #include。Boot 离开内核、内核离开 Boot，都要能单独编译。
 * 改一份必须改另一份，并让两边的 _Static_assert 仍然通过。
 *
 * Boot 的 EDK2 编译已经定义 EFIAPI，整数类型来自 Uefi.h，不会走到 BootTypes.h。
 * 内核没有 EFIAPI，整数类型来自 BootTypes.h。
 * EntryAddress 是入口地址那个数，不是 KernelEntry.S 里的函数名。
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
    /*
     * 已废弃：曾表示 Boot 扫到过 BLOCKS.ID。内核不读；认盘靠 BootDiskGuid，
     * 系统卷靠挂载后再找标记。字段保留以免改 ABI 尺寸/偏移。
     */
    UINT32               OsIdSeen;
    /* UEFI_BOOT_DISK_GUID_MAGIC：BootDiskGuid 是加载本 EFI 的那块盘 */
    UINT32               OsIdPad;
    UINT8                BootDiskGuid[16];
} UEFI_BOOT_CONFIG;

#define UEFI_BOOT_DISK_GUID_MAGIC 0x314B5344u

#if defined(__GNUC__)
_Static_assert(sizeof(UEFI_VIDEO_CONFIG) == 32, "UEFI_VIDEO_CONFIG size");
_Static_assert(sizeof(UEFI_MEMORY_MAP) == 40, "UEFI_MEMORY_MAP size");
_Static_assert(sizeof(UEFI_VIDEO_MODE) == 16, "UEFI_VIDEO_MODE size");
_Static_assert(sizeof(UEFI_BOOT_CONFIG) == 656, "UEFI_BOOT_CONFIG size");
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
