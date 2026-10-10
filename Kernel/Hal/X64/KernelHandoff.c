/*
 * KernelHandoff.c — X64：把 UEFI 交接块翻译成 BOOT_INFO
 *
 * 【初学者】
 * - Hal/X64：KernelEntry 之后第一棒 C；译 UEFI_BOOT_CONFIG → BOOT_INFO。
 * - 入口：KernelHandoff；然后 KernelMain(&Info)。
 * - 边界：不 include UEFI 头；内存类型数字本地定义。
 */
#include "BootConfig.h"
#include "BootInfoTypes.h"
#include "IdentityMap.h"
#include "Kernel.h"
#include "HalSerial.h"

extern char __kernel_end[];
extern void HalPlatformSetXhciFallback(UINT64 Address);
extern void HalPlatformSetRsdp(UINT64 Address);
extern void HalPlatformSetSystemTable(void *SystemTable);
extern void HalPlatformNoteRuntimeRange(UINT64 Phys, UINT64 Size);

/* EFI 内存类型（只取我们用到的几个数字，避免拉整份 UEFI 头） */
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

/* 核心翻译：Cfg（UEFI 味）→ Out（内核味） */
static void BootInfoFromUefi(BOOT_CONFIG *Cfg, BOOT_INFO *Out, BOOT_CONFIG *CfgPhys) {
    MEMORY_MAP *Map = &Cfg->MemoryMap;
    UINT8 *Base;
    UINTN Count;
    UINTN i;

    Out->FrameBufferBase = Cfg->VideoConfig.FrameBufferBase;
    Out->FrameBufferSize = Cfg->VideoConfig.FrameBufferSize;
    Out->HorizontalResolution = Cfg->VideoConfig.HorizontalResolution;
    Out->VerticalResolution = Cfg->VideoConfig.VerticalResolution;
    Out->PixelsPerScanLine = Cfg->VideoConfig.PixelsPerScanLine;
    Out->RegionCount = 0;
    /* Kernel.elf 约定加载到 1MiB；结尾由链接脚本符号给出 */
    Out->KernelStart = 0x100000;
    Out->KernelEnd = (UINT64)(UINTN)__kernel_end;
    Out->VideoModeCount = 0;
    Out->GopProtocol = 0;
    Out->DtbPhys = 0;
    Out->XhciBase = Cfg->XhciBaseAddress;
    Out->OsIdSeen = Cfg->OsIdSeen ? 1u : 0u;
    {
        UINT32 n;
        UINT32 N = Cfg->VideoModeCount;

        if (N > UEFI_VIDEO_MODE_MAX) {
            N = UEFI_VIDEO_MODE_MAX;
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
                /*
                 * 仍高于 4GiB 恒等窗的 Boot/Loader 页：先记账，
                 * 以后按需映射（例如某些 GOP SetMode 缓冲）。
                 */
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

    /* 明确保留：IVT/BDA 附近、AP 启动蹦床、内核、交接块、内存图、FB */
    BootInfoAddRegion(Out, 0, 4096, 0);
    BootInfoAddRegion(Out, 0x7000, 0x2000, 0);
    BootInfoAddRegion(Out, Out->KernelStart, Out->KernelEnd - Out->KernelStart, 0);
    if (CfgPhys) {
        BootInfoAddRegion(Out, (UINT64)(UINTN)CfgPhys, sizeof(BOOT_CONFIG), 0);
    }
    if (Map->Buffer != 0 && Map->MapSize != 0) {
        BootInfoAddRegion(Out, (UINT64)(UINTN)Map->Buffer, Map->MapSize, 0);
    }
    if (Out->FrameBufferSize != 0) {
        BootInfoAddRegion(Out, Out->FrameBufferBase, Out->FrameBufferSize, 0);
    }
}

void KernelHandoff(BOOT_CONFIG *BootConfig) {
    static BOOT_INFO Info;
    static BOOT_CONFIG CfgCopy;

    if (BootConfig == 0) {
        for (;;) {
        }
    }

    CfgCopy = *BootConfig;
    BootInfoFromUefi(&CfgCopy, &Info, &CfgCopy);
    HalSerialInitialize();
    HalSerialWrite("handoff: BOOT_INFO ready\n");
    KernelMain(&Info);
    for (;;) {
    }
}
