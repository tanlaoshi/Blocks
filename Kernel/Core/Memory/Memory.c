/*
 * Memory.c — Memory 模块表项：PhysicalMemory + 自检
 *
 * 【初学者】
 * - Core 层：开机模块表里「Memory」一行，只做 PMM 初始化与一页自检。
 * - 入口：MemoryInitialize（ModulesRunFull 调用）。
 * - 边界：不建页表；虚存见 VirtualMemory/。
 */
#include "Memory.h"
#include "PhysicalMemory.h"
#include "HalSerial.h"
#include "SerialConfig.h"

/*
 * MemoryInitialize — Memory 模块表入口
 *
 * 做什么：PhysicalMemoryInitialize + 分配/写/释放一页自检。
 * 谁调用：ModulesRunFull（Memory 行）。
 * 前后文：前 — BootInfo 已 Save；后 — VirtualMemoryInitialize。
 * 返回：0 成功；-1 PMM 或自检失败。
 */
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
        HalSerialWriteChannel(SLOG_MEM, "PMM: self-test alloc failed\n");
        return -1;
    }
    *(volatile UINT32 *)Page = 0x504D4D31u;
    if (*(volatile UINT32 *)Page != 0x504D4D31u) {
        HalSerialWriteChannel(SLOG_MEM, "PMM: self-test write failed\n");
        return -1;
    }
    PhysicalMemoryFreePage(Page);
    FreeAfter = PhysicalMemoryFreePageCount();
    if (FreeAfter != FreeBefore) {
        HalSerialWriteChannel(SLOG_MEM, "PMM: self-test free mismatch\n");
        return -1;
    }
    HalSerialWriteChannel(SLOG_MEM, "PMM: self-test ok\n");
    return 0;
}
