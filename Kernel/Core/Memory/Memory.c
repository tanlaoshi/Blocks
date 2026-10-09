/*
 * Memory.c — Memory 模块表项：PhysicalMemory + 自检
 */
#include "Memory.h"
#include "PhysicalMemory.h"
#include "HalSerial.h"
#include "ToySerialConfig.h"

int MemoryInitialize(void) {
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
