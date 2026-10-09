/*
 * Cpu.c — Cpu 模块表项（委托 HalCpuInitialize）
 */
#include "Cpu.h"
#include "HalCpu.h"
#include "HalSerial.h"
#include "ToySerialConfig.h"

int CpuInitialize(void) {
    if (HalCpuInitialize() != 0) {
        HalSerialWriteChannel(TOY_SLOG_MISC, "Cpu: init failed\n");
        return -1;
    }
#if defined(__x86_64__) || defined(_M_X64)
    HalSerialWriteChannel(TOY_SLOG_MISC, "Cpu: gdt/idt ok\n");
#else
    HalSerialWriteChannel(TOY_SLOG_MISC, "Cpu: stub ok\n");
#endif
    return 0;
}
