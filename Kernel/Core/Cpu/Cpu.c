/*
 * Cpu.c — Cpu 模块表项（委托 HalCpuInitialize）
 *
 * 【初学者】
 * - Core 层：GDT/IDT/Syscall 门等 CPU 设施在 HalCpu*。
 * - 入口：CpuInitialize（ModulesRunFull，在 Video 之后 USB 之前）。
 * - 边界：不实现中断服务；见 Hal/X64/HalCpu.c。
 */
#include "Cpu.h"
#include "HalCpu.h"
#include "HalSerial.h"
#include "SerialConfig.h"

/*
 * CpuInitialize — Cpu 模块表入口
 *
 * 做什么：HalCpuInitialize（GDT/IDT/ syscall 门等）。
 * 谁调用：ModulesRunFull（Video 之后）。
 * 返回：0 成功；-1 Hal 失败。
 */
int CpuInitialize(void) {
    if (HalCpuInitialize() != 0) {
        HalSerialWriteChannel(SLOG_MISC, "Cpu: init failed\n");
        return -1;
    }
#if defined(__x86_64__) || defined(_M_X64)
    HalSerialWriteChannel(SLOG_MISC, "Cpu: gdt/idt ok\n");
#else
    HalSerialWriteChannel(SLOG_MISC, "Cpu: stub ok\n");
#endif
    return 0;
}
