/*
 * HalCapability.c — 能力旗标与 CPU 停车（三架构合文件）
 *
 * 【初学者 · 为什么要旗标？】
 * 同一套 KernelMain 要跑在：
 *   - 课堂 PC / QEMU x86（通常有 GOP 屏，走「全量」模块表）
 *   - QEMU Arm/RiscV virt（可能只有串口，或有内存帧缓冲桌面）
 * 用「问问题」代替「猜架构」：
 *
 *   HalHasFrameBuffer()
 *     → BOOT_INFO 里 FrameBufferSize != 0 吗？
 *
 *   HalConsoleOnly()
 *     → X64：永远 0（课堂默认要桌面路径）
 *     → Arm/RiscV：没有 FB 就只能串口壳
 *
 *   HalPlatformIsVirtSerialConsole()
 *     → 是不是 virt 平台形状（影响选哪张模块表）
 *     → X64：0；Arm/RiscV：1
 *
 * HalCpuPark()：让当前 CPU 停住等中断（x86: hlt；Arm: wfe；RiscV: wfi）。
 * 开机失败或 K0 收尾的死循环会调用它，避免空转烧满主机 CPU。
 */
#include "HalCapability.h"
#include "BootInfo.h"

int HalHasFrameBuffer(void) {
    const BOOT_INFO *Info = BootInfoGet();

    return (Info != 0 && Info->FrameBufferSize != 0) ? 1 : 0;
}

int HalConsoleOnly(void) {
#if defined(__x86_64__) || defined(_M_X64)
    return 0;
#else
    return HalHasFrameBuffer() ? 0 : 1;
#endif
}

int HalPlatformIsVirtSerialConsole(void) {
#if defined(__x86_64__) || defined(_M_X64)
    return 0;
#else
    return 1;
#endif
}

void HalCpuPark(void) {
#if defined(__x86_64__) || defined(_M_X64)
    __asm__ volatile("cli; hlt");
#elif defined(__aarch64__)
    __asm__ volatile("wfe");
#elif defined(__riscv)
    __asm__ volatile("wfi");
#else
    for (;;) {
    }
#endif
}
