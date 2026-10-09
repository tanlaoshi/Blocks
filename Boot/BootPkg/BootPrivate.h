/*
 * BootPrivate.h ? BootPkg ???????? .c ???
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

/* Video?GetVideoInfo / SetVideoMode?????? VideoScore / Edid / Theme? */
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
EFI_STATUS GetVideoInfo(EFI_HANDLE ImageHandle, UEFI_BOOT_CONFIG *BootConfig);
EFI_STATUS SetVideoMode(EFI_HANDLE ImageHandle, VIDEO_CONFIG *VideoConfig,
                        UEFI_BOOT_CONFIG *BootConfig);

/* LoadKernel */
BOOLEAN FsHasOsMarker(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs);
EFI_STATUS ReadKernelFile(EFI_HANDLE ImageHandle, EFI_PHYSICAL_ADDRESS *OutBuffer,
                          UINTN *OutSize);
EFI_STATUS CheckAndLoadKernel(EFI_PHYSICAL_ADDRESS ElfBase, UINTN FileSize,
                              EFI_PHYSICAL_ADDRESS *EntryPoint);
EFI_STATUS BootLoadKernel(EFI_HANDLE ImageHandle, UEFI_BOOT_CONFIG *BootConfig);

/* FillRsdp / FillXhci / JumpToKernel */
VOID BootFillRsdp(UEFI_BOOT_CONFIG *BootConfig);
EFI_STATUS GetXhciBaseAddress(UINT64 *XhciBase);
VOID BootFillXhci(UEFI_BOOT_CONFIG *BootConfig);
EFI_STATUS JumpToKernel(EFI_HANDLE ImageHandle, UEFI_BOOT_CONFIG *BootConfig);

#endif
