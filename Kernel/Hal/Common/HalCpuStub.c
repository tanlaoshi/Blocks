/*
 * HalCpuStub.c — Arm/RiscV：Cpu 模块成功桩（K7）
 *
 * X64 链 Hal/X64/HalCpu.c；本文件只保证三架构可编、模块表能过。
 */
#include "HalCpu.h"

int HalCpuInitialize(void) {
    return 0;
}

void HalCpuIdtSet(UINT32 Vec, void *Handler) {
    (void)Vec;
    (void)Handler;
}
