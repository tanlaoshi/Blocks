/*
 * HandoffMemory.c — UEFI 内存图 → BOOT_INFO 区段；记 Runtime / 高址 Loader
 */
#include "HandoffPrivate.h"
#include "IdentityMap.h"

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

int HandoffAddRegion(BOOT_INFO *Info, UINT64 Phys, UINT64 Size, int Free) {
    if (Info->RegionCount >= BOOT_MEMORY_REGIONS_MAX || Size == 0) {
        return -1;
    }
    Info->Regions[Info->RegionCount].Phys = Phys;
    Info->Regions[Info->RegionCount].Size = Size;
    Info->Regions[Info->RegionCount].Free = Free ? 1u : 0u;
    Info->RegionCount++;
    return 0;
}

void HandoffFillMemory(const UEFI_BOOT_CONFIG *Cfg, BOOT_INFO *Out,
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
            HandoffAddRegion(Out, Desc->PhysicalStart, Desc->NumberOfPages << 12, 1);
        }
    }

    HandoffAddRegion(Out, 0, 4096, 0);
    HandoffAddRegion(Out, 0x7000, 0x2000, 0);
    /*
     * 内核两段：低端 text/data 与 32MiB BSS（中间空洞留给 PMM，勿整跨度保留）。
     */
    {
        extern char __kernel_start[];
        extern char __kernel_data_end[];
        extern char __kernel_bss_start[];
        extern char __kernel_end[];
        UINT64 Lo = (UINT64)(UINTN)__kernel_start;
        UINT64 Mid = (UINT64)(UINTN)__kernel_data_end;
        UINT64 Bss = (UINT64)(UINTN)__kernel_bss_start;
        UINT64 Hi = (UINT64)(UINTN)__kernel_end;

        if (Mid > Lo) {
            HandoffAddRegion(Out, Lo, Mid - Lo, 0);
        }
        if (Hi > Bss) {
            HandoffAddRegion(Out, Bss, Hi - Bss, 0);
        }
    }
    if (CfgPhys) {
        HandoffAddRegion(Out, (UINT64)(UINTN)CfgPhys, sizeof(UEFI_BOOT_CONFIG), 0);
    }
    if (Map->Buffer != 0 && Map->MapSize != 0) {
        HandoffAddRegion(Out, (UINT64)(UINTN)Map->Buffer, Map->MapSize, 0);
    }
    if (Out->FrameBufferSize != 0) {
        HandoffAddRegion(Out, Out->FrameBufferBase, Out->FrameBufferSize, 0);
    }
}
