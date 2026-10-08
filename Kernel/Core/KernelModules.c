/*
 * KernelModules.c — 开机模块表（积木拼表）
 *
 * 【初学者】
 * 每一行 = 一块要启动的积木：名字 + 初始化函数。
 * 顺序就是依赖顺序（Memory 在 VirtualMemory 前）。
 *
 * Full 表：Serial → Memory（K4）。
 */
#include "KernelModules.h"
#include "Module.h"
#include "HalSerial.h"
#include "PhysicalMemory.h"
#include "ToySerialConfig.h"

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
    /* 恒等窗内：可直接写 */
    *(volatile UINT32 *)Page = 0x504D4D31u; /* 'PMM1' */
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

static const MODULE gModulesFull[] = {
    { "Serial", InitializeSerial },
    { "Memory", InitializeMemory },
};

#define MODULE_COUNT(Table) ((int)(sizeof(Table) / sizeof((Table)[0])))

int KernelModulesRunFull(void) {
    return ModulesRun(gModulesFull, MODULE_COUNT(gModulesFull));
}
