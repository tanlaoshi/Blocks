/*
 * BootPrivate.h — BootPkg 内部声明（各 .c 共用）
 */
#ifndef BOOT_PRIVATE_H
#define BOOT_PRIVATE_H

#include <Uefi.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/SimpleFileSystem.h>

#include "UefiBootConfig.h"
#include "Serial.h"

#ifndef BOOT_DEBUG
#define BOOT_DEBUG 0
#endif
#if BOOT_DEBUG
#define BootDbg(...) BootSerialPrintf(__VA_ARGS__)
#else
#define BootDbg(...) do { } while (0)
#endif

typedef UEFI_VIDEO_CONFIG VIDEO_CONFIG;
typedef UEFI_MEMORY_MAP   MEMORY_MAP;

/*
 * 选模分值：数字越大越优先。优先级从高到低——
 *
 *   SCORE_KEEP_CURRENT   保留固件当前模式（无 mode= 时；不再切模）
 *   SCORE_CFG_EXACT      BLOCKS.DB 的 mode= 精确命中
 *   SCORE_NATIVE_EXACT   与目标（EDID）同宽高
 *   SCORE_SAME_ASPECT    同宽高比，按距离就近
 *   SCORE_ASPECT_FALLBACK 请求的宽高比不在 GOP 里时，跟显示器宽高比
 *   SCORE_NEAREST        其它，按距离就近
 *
 * 距离项：每差 1 像素扣 SCORE_DIST_STEP，距离上限 SCORE_DISTANCE_CAP；
 * 面积项：像素数 / SCORE_AREA_DIV 作为同档内的次级排序。
 * 改这些常量前先看 Score.c 的注释表，别让两处分值域打架。
 */
#define SCORE_KEEP_CURRENT    6000000000ULL
#define SCORE_CFG_EXACT       5000000000ULL
#define SCORE_NATIVE_EXACT    3000000000ULL
#define SCORE_SAME_ASPECT     2000000000ULL
#define SCORE_ASPECT_FALLBACK 1500000000ULL
#define SCORE_NEAREST         1000000000ULL
#define SCORE_DISTANCE_CAP    100000
#define SCORE_DIST_STEP       1000ULL
#define SCORE_AREA_DIV        10000ULL

/* Video/：GetVideoInfo / SetVideoMode；辅助 Score / Edid / Theme */
/*
 * 选模结果：GetVideoInfo 填，SetVideoMode 读。
 * 以前这是 13 个文件级 static，签名上看不出「必须先 Get 再 Set」。
 */
typedef struct {
    EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;
    UINTN   BestMode;
    UINT32  BestW;
    UINT32  BestH;
    UINTN   BestScore;
    BOOLEAN InVm;
    BOOLEAN HasCfgTarget;
    BOOLEAN CfgMatched;
    BOOLEAN HasEdidTarget;
    UINT32  CfgW;
    UINT32  CfgH;
    UINT32  EdidW;
    UINT32  EdidH;
    UINT32  ModeCount;
} VIDEO_SELECTION;

BOOLEAN IsVirtualMachine(VOID);
BOOLEAN IsModeUsable(const EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info);
EFI_STATUS TryGetEdidPreferred(EFI_HANDLE ImageHandle, EFI_HANDLE GopHandle,
                               UINT32 *PrefW, UINT32 *PrefH);
UINTN ScoreModeQemu(UINT32 W, UINT32 H);
BOOLEAN SameAspectRatio(UINT32 W, UINT32 H, UINT32 TargetW, UINT32 TargetH);
UINTN ScoreModeNative(UINT32 W, UINT32 H, UINT32 TargetW, UINT32 TargetH,
                      BOOLEAN HasTarget);
VOID SortVideoModesForSettings(UEFI_VIDEO_MODE *Modes, UINT32 Count,
                               BOOLEAN InVm, BOOLEAN HasEdid,
                               UINT32 EdidW, UINT32 EdidH);
BOOLEAN TryLoadDisplayPref(EFI_HANDLE ImageHandle, UINT32 *OutW, UINT32 *OutH);
EFI_STATUS GetVideoInfo(EFI_HANDLE ImageHandle, UEFI_BOOT_CONFIG *BootConfig,
                        VIDEO_SELECTION *Sel);
EFI_STATUS SetVideoMode(EFI_HANDLE ImageHandle, VIDEO_CONFIG *VideoConfig,
                        UEFI_BOOT_CONFIG *BootConfig,
                        const VIDEO_SELECTION *Sel);
/* Volume：BLOCKS.ID、启动盘 GUID、同盘判定、ROOT.CFG 三档认卷 */
BOOLEAN FsHasOsMarker(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs);
BOOLEAN BootSamePhysicalDisk(EFI_HANDLE A, EFI_HANDLE B);
VOID BootFillDiskGuid(EFI_HANDLE ImageHandle, UEFI_BOOT_CONFIG *BootConfig);
/*
 * Pass：0=ESP ROOT.CFG(disk+part) 且 BLOCKS.ID；1=同盘+ID；2=它盘+ID；3=启动卷。
 * ReadKernel / Theme 共用，避免两套规则。
 */
BOOLEAN BootOsVolForPass(EFI_HANDLE ImageHandle, EFI_HANDLE VolHandle, UINTN Pass);

/* LoadKernel/：Read / Elf / 编排 */
EFI_STATUS ReadKernelFile(EFI_HANDLE ImageHandle, EFI_PHYSICAL_ADDRESS *OutBuffer,
                          UINTN *OutSize);
EFI_STATUS CheckKernelFile(EFI_PHYSICAL_ADDRESS ElfBase, UINTN FileSize,
                           EFI_PHYSICAL_ADDRESS *LoadLow, UINTN *PageCount);
EFI_STATUS LoadKernelFile(EFI_PHYSICAL_ADDRESS *ElfBase, UINTN FileSize,
                          EFI_PHYSICAL_ADDRESS LoadLow, UINTN PageCount,
                          EFI_PHYSICAL_ADDRESS *EntryPoint);
EFI_STATUS BootLoadKernel(EFI_HANDLE ImageHandle, UEFI_BOOT_CONFIG *BootConfig);

/* FillRsdp / FillXhci / JumpToKernel */
VOID BootFillRsdp(UEFI_BOOT_CONFIG *BootConfig);
EFI_STATUS GetXhciBaseAddress(UINT64 *XhciBase);
VOID BootFillXhci(UEFI_BOOT_CONFIG *BootConfig);
EFI_STATUS JumpToKernel(EFI_HANDLE ImageHandle, UEFI_BOOT_CONFIG *BootConfig);

#endif
