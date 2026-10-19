/*
 * KernelHandoff.c — BringUp/X64：UEFI_BOOT_CONFIG → BOOT_INFO → KernelMain
 *
 * 入口栈切换在同目录 KernelEntry.S；本文件译包、开早期恒等，再交棒 KernelMain。
 * 交接包 UefiBootConfig.h 与 Boot/BootPkg/ 那份内容相同。
 */
#include "UefiBootConfig.h"
#include "BootInfo.h"
#include "IdentityMap.h"
#include "Kernel.h"
#include "HalSerial.h"
#include "EarlyIdentity.h"

extern char __kernel_end[];
extern void HalPlatformSetXhciFallback(UINT64 Address);
extern void HalPlatformSetRsdp(UINT64 Address);
extern void HalPlatformSetSystemTable(void *SystemTable);
extern void HalPlatformNoteRuntimeRange(UINT64 Phys, UINT64 Size);

#define EFI_LOADER_CODE         1
#define EFI_LOADER_DATA         2
#define EFI_BOOT_SERVICES_CODE  3
#define EFI_BOOT_SERVICES_DATA  4
#define EFI_MEMORY_CONVENTIONAL 7
#define EFI_MEMORY_RUNTIME      (1ULL << 63)

typedef struct {
    UINT32 Type;
    UINT32 Pad;
    UINT64 PhysicalStart;
    UINT64 VirtualStart;
    UINT64 NumberOfPages;
    UINT64 Attribute;
} EFI_MEMORY_DESCRIPTOR;

static int BootInfoAddRegion(BOOT_INFO *Info, UINT64 Phys, UINT64 Size, int Free) {
    if (Info->RegionCount >= BOOT_MEMORY_REGIONS_MAX || Size == 0) {
        return -1;
    }
    Info->Regions[Info->RegionCount].Phys = Phys;
    Info->Regions[Info->RegionCount].Size = Size;
    Info->Regions[Info->RegionCount].Free = Free ? 1u : 0u;
    Info->RegionCount++;
    return 0;
}

static void BootInfoFromUefi(UEFI_BOOT_CONFIG *Cfg, BOOT_INFO *Out, UEFI_BOOT_CONFIG *CfgPhys) {
    UEFI_MEMORY_MAP *Map = &Cfg->MemoryMap;
    UINT8 *Base;
    UINTN Count;
    UINTN i;

    Out->FrameBufferBase = Cfg->VideoConfig.FrameBufferBase;
    Out->FrameBufferSize = Cfg->VideoConfig.FrameBufferSize;
    Out->HorizontalResolution = Cfg->VideoConfig.HorizontalResolution;
    Out->VerticalResolution = Cfg->VideoConfig.VerticalResolution;
    Out->PixelsPerScanLine = Cfg->VideoConfig.PixelsPerScanLine;
    Out->RegionCount = 0;
    Out->KernelStart = 0x100000;
    Out->KernelEnd = (UINT64)(UINTN)__kernel_end;
    Out->VideoModeCount = 0;
    Out->GopProtocol = 0;
    {
        UINT32 n;
        UINT32 N = Cfg->VideoModeCount;

        if (N > BOOT_VIDEO_MODE_MAX) {
            N = BOOT_VIDEO_MODE_MAX;
        }
        for (n = 0; n < N; n++) {
            Out->VideoModes[n].Width = Cfg->VideoModes[n].Width;
            Out->VideoModes[n].Height = Cfg->VideoModes[n].Height;
            Out->VideoModes[n].ModeNumber = Cfg->VideoModes[n].ModeNumber;
        }
        Out->VideoModeCount = N;
        if (Cfg->VideoModePad == UEFI_GOP_HANDOFF_MAGIC &&
            Cfg->GopProtocol != 0) {
            Out->GopProtocol = Cfg->GopProtocol;
        }
    }
    Out->BootDiskGuidOk = 0;
    if (Cfg->OsIdPad == UEFI_BOOT_DISK_GUID_MAGIC) {
        UINT32 b;
        for (b = 0; b < 16; b++) {
            Out->BootDiskGuid[b] = Cfg->BootDiskGuid[b];
        }
        Out->BootDiskGuidOk = 1;
    }

    HalPlatformSetXhciFallback(Cfg->XhciBaseAddress);
    HalPlatformSetRsdp(Cfg->RsdpAddress);
    HalPlatformSetSystemTable(Cfg->SystemTable);

    if (Map->Buffer != 0 && Map->DescriptorSize >= sizeof(EFI_MEMORY_DESCRIPTOR)) {
        Base = (UINT8 *)Map->Buffer;
        Count = Map->MapSize / Map->DescriptorSize;
        for (i = 0; i < Count; i++) {
            EFI_MEMORY_DESCRIPTOR *Desc =
                (EFI_MEMORY_DESCRIPTOR *)(Base + i * Map->DescriptorSize);
            if (Desc->Attribute & EFI_MEMORY_RUNTIME) {
                HalPlatformNoteRuntimeRange(Desc->PhysicalStart,
                                            Desc->NumberOfPages << 12);
            } else if (Desc->PhysicalStart >= IDENTITY_BYTES &&
                       (Desc->Type == EFI_LOADER_CODE ||
                        Desc->Type == EFI_LOADER_DATA ||
                        Desc->Type == EFI_BOOT_SERVICES_CODE ||
                        Desc->Type == EFI_BOOT_SERVICES_DATA)) {
                HalPlatformNoteRuntimeRange(Desc->PhysicalStart,
                                            Desc->NumberOfPages << 12);
            }
            if (Desc->Type != EFI_MEMORY_CONVENTIONAL) {
                continue;
            }
            BootInfoAddRegion(Out, Desc->PhysicalStart,
                              Desc->NumberOfPages << 12, 1);
        }
    }

    BootInfoAddRegion(Out, 0, 4096, 0);
    BootInfoAddRegion(Out, 0x7000, 0x2000, 0);
    BootInfoAddRegion(Out, Out->KernelStart, Out->KernelEnd - Out->KernelStart, 0);
    if (CfgPhys) {
        BootInfoAddRegion(Out, (UINT64)(UINTN)CfgPhys, sizeof(UEFI_BOOT_CONFIG), 0);
    }
    if (Map->Buffer != 0 && Map->MapSize != 0) {
        BootInfoAddRegion(Out, (UINT64)(UINTN)Map->Buffer, Map->MapSize, 0);
    }
    if (Out->FrameBufferSize != 0) {
        BootInfoAddRegion(Out, Out->FrameBufferBase, Out->FrameBufferSize, 0);
    }
}

void KernelHandoff(UEFI_BOOT_CONFIG *BootConfig) {
    static BOOT_INFO Info;
    static UEFI_BOOT_CONFIG CfgCopy;

    if (BootConfig == 0) {
        for (;;) {
        }
    }

    CfgCopy = *BootConfig;
    BootInfoFromUefi(&CfgCopy, &Info, &CfgCopy);
    BootInfoSet(&Info);
    HalSerialInitialize();
    /* PMM/VMM 前开 4GiB 恒等；栈已在 KernelEntry 换到窗内 */
    if (EarlyIdentitySetup() == 0) {
        EarlyIdentityEnable();
    }
    KernelMain();
    for (;;) {
    }
}
