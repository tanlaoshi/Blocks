/*
 * KernelHandoff.c — BringUp/X64：UEFI_BOOT_CONFIG → BOOT_HANDOFF → KernelMain
 *
 * 入口栈切换在同目录 KernelEntry.S。
 * 交接包 UefiBootConfig.h 与 Boot/BootPkg/ 那份内容相同。
 */
#include "UefiBootConfig.h"
#include "BootHandoff.h"
#include "Kernel.h"
#include "Hal.h"
#include "HalSerial.h"
#include "SerialLog.h"
#include "EarlyIdentity.h"
#include "IdentityMap.h"

extern char __kernel_start[];
extern char __kernel_end[];
extern char __kernel_data_end[];
extern char __kernel_bss_start[];

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

/* Video / Modes / Gop / BootDiskGuid → BOOT_HANDOFF（不含内存图） */
static void FillHandoff(const UEFI_BOOT_CONFIG *UefiBootConfig, BOOT_HANDOFF *BootHandoff) {
    UINT32 ModeIndex;
    UINT32 VideoModeCount;

    BootHandoff->FrameBufferBase = UefiBootConfig->VideoConfig.FrameBufferBase;
    BootHandoff->FrameBufferSize = UefiBootConfig->VideoConfig.FrameBufferSize;
    BootHandoff->HorizontalResolution = UefiBootConfig->VideoConfig.HorizontalResolution;
    BootHandoff->VerticalResolution = UefiBootConfig->VideoConfig.VerticalResolution;
    BootHandoff->PixelsPerScanLine = UefiBootConfig->VideoConfig.PixelsPerScanLine;
    BootHandoff->RegionCount = 0;
    /* Start=低端 text；End=高位 BSS 末（段布局见 link.ld / FillHandoffRegions） */
    BootHandoff->KernelStart = (UINT64)(UINTN)__kernel_start;
    BootHandoff->KernelEnd = (UINT64)(UINTN)__kernel_end;
    BootHandoff->VideoModeCount = 0;
    BootHandoff->GopProtocol = 0;

    VideoModeCount = UefiBootConfig->VideoModeCount;
    if (VideoModeCount > BOOT_VIDEO_MODE_MAX) {
        VideoModeCount = BOOT_VIDEO_MODE_MAX;
    }
    for (ModeIndex = 0; ModeIndex < VideoModeCount; ModeIndex++) {
        BootHandoff->VideoModes[ModeIndex].Width = UefiBootConfig->VideoModes[ModeIndex].Width;
        BootHandoff->VideoModes[ModeIndex].Height = UefiBootConfig->VideoModes[ModeIndex].Height;
        BootHandoff->VideoModes[ModeIndex].ModeNumber = UefiBootConfig->VideoModes[ModeIndex].ModeNumber;
    }
    BootHandoff->VideoModeCount = VideoModeCount;
    if (UefiBootConfig->VideoModePad == UEFI_GOP_HANDOFF_MAGIC && UefiBootConfig->GopProtocol != 0) {
        BootHandoff->GopProtocol = UefiBootConfig->GopProtocol;
    }

    BootHandoff->BootDiskGuidOk = 0;
    if (UefiBootConfig->OsIdPad == UEFI_BOOT_DISK_GUID_MAGIC) {
        UINT32 GuidIndex;
        for (GuidIndex = 0; GuidIndex < 16; GuidIndex++) {
            BootHandoff->BootDiskGuid[GuidIndex] = UefiBootConfig->BootDiskGuid[GuidIndex];
        }
        BootHandoff->BootDiskGuidOk = 1;
    }
}

/* XHCI / RSDP / SystemTable → HalPlatform* */
static void FillHalPlatform(const UEFI_BOOT_CONFIG *Cfg) {
    HalPlatformSetXhciFallback(Cfg->XhciBaseAddress);
    HalPlatformSetRsdp(Cfg->RsdpAddress);
    HalPlatformSetSystemTable(Cfg->SystemTable);
}

static int AddRegion(BOOT_HANDOFF *Handoff, UINT64 Phys, UINT64 Size, int Free) {
    if (Handoff->RegionCount >= BOOT_MEMORY_REGIONS_MAX || Size == 0) {
        return -1;
    }
    Handoff->Regions[Handoff->RegionCount].Phys = Phys;
    Handoff->Regions[Handoff->RegionCount].Size = Size;
    Handoff->Regions[Handoff->RegionCount].Free = Free ? 1u : 0u;
    Handoff->RegionCount++;
    return 0;
}

/* UEFI 内存图 + 内核/FB 保留 → BOOT_HANDOFF.Regions[]（PMM 原料，不分配） */
static void FillHandoffRegions(const UEFI_BOOT_CONFIG *Cfg, BOOT_HANDOFF *Out,
                               const UEFI_BOOT_CONFIG *CfgPhys) {
    const UEFI_MEMORY_MAP *Map = &Cfg->MemoryMap;
    UINT8 *Base;
    UINTN Count;
    UINTN i;

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
            AddRegion(Out, Desc->PhysicalStart, Desc->NumberOfPages << 12, 1);
        }
    }

    AddRegion(Out, 0, 4096, 0);
    AddRegion(Out, 0x7000, 0x2000, 0);
    /*
     * 内核两段：低端 text/data 与 32MiB BSS（中间空洞留给 PMM，勿整跨度保留）。
     */
    {
        UINT64 Lo = (UINT64)(UINTN)__kernel_start;
        UINT64 Mid = (UINT64)(UINTN)__kernel_data_end;
        UINT64 Bss = (UINT64)(UINTN)__kernel_bss_start;
        UINT64 Hi = (UINT64)(UINTN)__kernel_end;

        if (Mid > Lo) {
            AddRegion(Out, Lo, Mid - Lo, 0);
        }
        if (Hi > Bss) {
            AddRegion(Out, Bss, Hi - Bss, 0);
        }
    }
    if (CfgPhys) {
        AddRegion(Out, (UINT64)(UINTN)CfgPhys, sizeof(UEFI_BOOT_CONFIG), 0);
    }
    if (Map->Buffer != 0 && Map->MapSize != 0) {
        AddRegion(Out, (UINT64)(UINTN)Map->Buffer, Map->MapSize, 0);
    }
    if (Out->FrameBufferSize != 0) {
        AddRegion(Out, Out->FrameBufferBase, Out->FrameBufferSize, 0);
    }
}

void KernelHandoff(UEFI_BOOT_CONFIG *BootConfig) {
    static BOOT_HANDOFF BootHandoff;
    static UEFI_BOOT_CONFIG UefiBootConfig;

    /* 与 Fill* 无关；尽早开串口。空 BootConfig* 已在 KernelEntry 拦下 */
    HalSerialInitialize();

    UefiBootConfig = *BootConfig;
    FillHandoff(&UefiBootConfig, &BootHandoff);
    FillHalPlatform(&UefiBootConfig);
    FillHandoffRegions(&UefiBootConfig, &BootHandoff, &UefiBootConfig);
    /* PMM/VMM 前：搭 4GiB 恒等页表并开 PG；栈已在 KernelEntry 换到窗内 */
    EnableEarlyIdentityMap();
    BootHandoffSet(&BootHandoff);

    KernelMain();
    /* 正常不返回；尝试关机（QEMU/ACPI），失败则 HalCpuShutdown 内 hlt */
    LogBoot("Boot: KernelMain returned (shutdown)\n");
    HalCpuShutdown();
}
