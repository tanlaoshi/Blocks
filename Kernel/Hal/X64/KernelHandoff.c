/*
 * KernelHandoff.c — X64：把 UEFI 交接块翻译成 BOOT_INFO
 *
 * 【初学者 · 本文件在流水线中的位置】
 *   KernelEntry.S（已换好早期栈）
 *     → KernelHandoff(UEFI_BOOT_CONFIG*)
 *         1) 拷贝交接块到内核 BSS（不要继续用 UEFI 栈上的指针）
 *         2) BootInfoFromUefi：内存图 / 帧缓冲 / 保留区 → BOOT_INFO
 *         3) HalSerialInitialize + 打一行 handoff 日志
 *         4) KernelMain(&Info)  —— 之后三架构合流
 *
 * 【什么是 UEFI_BOOT_CONFIG？】
 * ToyBoot 在退出 Boot Services 前打包的结构（权威布局在 Boot/BootPkg）。
 * 里面有 GOP 帧缓冲、GetMemoryMap 结果、可选 xHCI/RSDP 等。
 * Common 内核不直接 include UEFI 头，所以要在这里「翻译」。
 *
 * 【内存图在干什么？】
 * EFI 描述符类型很多。我们只把 ConventionalMemory 标成 Free=1 给以后 PMM；
 * 内核映像、帧缓冲、交接块本身标成保留。Runtime 页记下来供以后映射。
 *
 * 入口栈已由汇编设好；这里是普通 C，不要再改 rsp。
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
        if (Cfg->VideoModePad == TOY_BOOT_GOP_HANDOFF_MAGIC &&
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
            } else if (Desc->PhysicalStart >= TOY_IDENTITY_BYTES &&
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
