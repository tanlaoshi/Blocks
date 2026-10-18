/*
 * Initialize.c — InitializeVirtualMemory（开机模块表）
 *
 * 建页表，映上帧缓冲和设备寄存器，再打开分页。
 */
#include "KernelModulesPrivate.h"
#include "BootInfo.h"
#include "HalPlatform.h"
#include "PhysicalMemory.h"
#include "VirtualMemory.h"

static int MapIdentity(UINT64 Phys, UINT64 Size) {
    UINT64 Start;
    UINT64 End;

    if (Size == 0) {
        return 0;
    }
    Start = Phys & ~(UINT64)(PAGE_SIZE - 1);
    End = Phys + Size;
    while (Start < End) {
        if (VirtualMemoryMapPage(Start, Start, PTE_PRESENT | PTE_WRITABLE) != 0) {
            return -1;
        }
        Start += PAGE_SIZE;
    }
    return 0;
}

int InitializeVirtualMemory(void) {
    const BOOT_INFO *Info = BootInfoGet();
    UINT64 FbBytes;
    UINT64 Layout;

    if (VirtualMemoryInitialize() != 0) {
        return -1;
    }
    if (Info && Info->FrameBufferBase != 0) {
        FbBytes = Info->FrameBufferSize;
        Layout = 0;
        if (Info->PixelsPerScanLine != 0 && Info->VerticalResolution != 0) {
            Layout = (UINT64)Info->PixelsPerScanLine *
                     (UINT64)Info->VerticalResolution * 4ull;
        }
        if (Layout > FbBytes) {
            FbBytes = Layout;
        }
        if (FbBytes != 0) {
            if (MapIdentity(Info->FrameBufferBase, FbBytes) != 0) {
                return -1;
            }
        }
    }
    HalPlatformMapMmio();
    VirtualMemoryEnable();
    return 0;
}
