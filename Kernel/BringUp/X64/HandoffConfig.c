/*
 * HandoffConfig.c — UEFI_BOOT_CONFIG 显示与盘 GUID → BOOT_INFO
 */
#include "HandoffPrivate.h"

extern char __kernel_end[];

void HandoffFillConfig(const UEFI_BOOT_CONFIG *Cfg, BOOT_INFO *Out) {
    UINT32 n;
    UINT32 N;

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

    N = Cfg->VideoModeCount;
    if (N > BOOT_VIDEO_MODE_MAX) {
        N = BOOT_VIDEO_MODE_MAX;
    }
    for (n = 0; n < N; n++) {
        Out->VideoModes[n].Width = Cfg->VideoModes[n].Width;
        Out->VideoModes[n].Height = Cfg->VideoModes[n].Height;
        Out->VideoModes[n].ModeNumber = Cfg->VideoModes[n].ModeNumber;
    }
    Out->VideoModeCount = N;
    if (Cfg->VideoModePad == UEFI_GOP_HANDOFF_MAGIC && Cfg->GopProtocol != 0) {
        Out->GopProtocol = Cfg->GopProtocol;
    }

    Out->BootDiskGuidOk = 0;
    if (Cfg->OsIdPad == UEFI_BOOT_DISK_GUID_MAGIC) {
        UINT32 b;
        for (b = 0; b < 16; b++) {
            Out->BootDiskGuid[b] = Cfg->BootDiskGuid[b];
        }
        Out->BootDiskGuidOk = 1;
    }
    (void)Cfg->OsIdSeen; /* 已废弃，不进 BOOT_INFO */
}
