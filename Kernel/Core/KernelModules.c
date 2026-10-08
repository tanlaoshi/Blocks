/*
 * KernelModules.c — 开机模块表（积木拼表）
 *
 * Full：Serial → Memory → Driver → VirtualMemory（K5）
 */
#include "KernelModules.h"
#include "Module.h"
#include "HalSerial.h"
#include "PhysicalMemory.h"
#include "Device.h"
#include "VirtualMemory.h"
#include "ToySerialConfig.h"
#include "HalVideo.h"
#include "BootInfo.h"

static int InitializeSerial(void) {
    HalSerialInitialize();
    return 0;
}

static int InitializeMemory(void) {
    void *Page;
    UINT64 FreeBefore;
    UINT64 FreeAfter;

    if (PhysicalMemoryInitialize() != 0) {
        return -1;
    }
    FreeBefore = PhysicalMemoryFreePageCount();
    Page = PhysicalMemoryAllocatePage();
    if (Page == 0) {
        HalSerialWriteChannel(TOY_SLOG_MEM, "PMM: self-test alloc failed\n");
        return -1;
    }
    *(volatile UINT32 *)Page = 0x504D4D31u;
    if (*(volatile UINT32 *)Page != 0x504D4D31u) {
        HalSerialWriteChannel(TOY_SLOG_MEM, "PMM: self-test write failed\n");
        return -1;
    }
    PhysicalMemoryFreePage(Page);
    FreeAfter = PhysicalMemoryFreePageCount();
    if (FreeAfter != FreeBefore) {
        HalSerialWriteChannel(TOY_SLOG_MEM, "PMM: self-test free mismatch\n");
        return -1;
    }
    HalSerialWriteChannel(TOY_SLOG_MEM, "PMM: self-test ok\n");
    return 0;
}

static int InitializeDriver(void) {
    DeviceInitialize();
    DeviceEnumerateAll();
    HalSerialWriteChannel(TOY_SLOG_DRV, "Driver: shell ok\n");
    return 0;
}

static int InitializeVirtualMemory(void) {
    const BOOT_INFO *Info;
    UINT32 W = 0;
    UINT32 H = 0;

    if (VirtualMemoryInitialize() != 0) {
        return -1;
    }
    VirtualMemoryEnable();

    /* 分页后仍能写 FB：左上角再画一绿点 */
    Info = BootInfoGet();
    HalVideoGetSize(&W, &H);
    if (Info != 0 && Info->FrameBufferSize != 0 && W > 16 && H > 16) {
        HalVideoDrawPixel(12, 12, 0x0000FF00u);
        HalSerialWriteChannel(TOY_SLOG_MEM, "VMM: FB pixel after PG ok\n");
    }
    return 0;
}

static const MODULE gModulesFull[] = {
    { "Serial", InitializeSerial },
    { "Memory", InitializeMemory },
    { "Driver", InitializeDriver },
    { "VirtualMemory", InitializeVirtualMemory },
};

#define MODULE_COUNT(Table) ((int)(sizeof(Table) / sizeof((Table)[0])))

int KernelModulesRunFull(void) {
    return ModulesRun(gModulesFull, MODULE_COUNT(gModulesFull));
}
