/*
 * HalCpuStub.c — 非 X64：最小 CPU 壳
 *
 * 【初学者】
 * - Hal 占位；CpuInitialize 仍成功，GDT/IDT 在 X64 真实现。
 * - 真实现：Hal/X64/HalCpu.c。
 */
#include "HalCpu.h"

int HalCpuInitialize(void) {
    return 0;
}

void HalCpuIdtSet(UINT32 Vec, void *Handler) {
    (void)Vec;
    (void)Handler;
}
