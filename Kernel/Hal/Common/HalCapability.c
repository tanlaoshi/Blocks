/*
 * HalCapability.c — 能力旗标与 CPU 停车（Hal/Common）
 *
 * 不 include BootInfo、不调用 BootInfoGet。
 * 有无帧缓冲由 KernelMain 经 HalCapabilityObserveFrameBuffer 注入。
 */
#include "HalCapability.h"

static int gHasFrameBuffer;

void HalCapabilityObserveFrameBuffer(UINT64 FrameBufferSize) {
    gHasFrameBuffer = (FrameBufferSize != 0) ? 1 : 0;
}

int HalHasFrameBuffer(void) {
    return gHasFrameBuffer;
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
