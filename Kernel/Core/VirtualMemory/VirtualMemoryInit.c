/*
 * VirtualMemoryInit.c — InitializeVirtualMemory（开机模块编织）
 *
 * 人话：建页表、映射帧缓冲与 MMIO，再开分页。实现仍在 Core/VirtualMemory（阶段 B 再并）。
 */
#include "KernelModulesPrivate.h"
#include "BootInfo.h"
#include "Hal.h"
#include "PhysicalMemory.h"
#include "VirtualMemory.h"

static int MapIdentity(UINT64 Phys, UINT64 Size) {
    if (Size == 0) {
        return 0;
    }
    UINT64 Start = Phys & ~(UINT64)(PAGE_SIZE - 1);
    UINT64 End = Phys + Size;
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

    if (VirtualMemoryInitialize() != 0) {
        return -1;
    }
    if (Info && Info->FrameBufferBase != 0) {
        UINT64 FbBytes = Info->FrameBufferSize;
        UINT64 Layout = 0;

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
