/*
 * HalFpuStub.c — 非 X64：无 SSE FPU 自检
 *
 * 【初学者】
 * - Hal 占位；Font TTF 路径在 Arm/RiscV 跳过 SSE。
 * - 真实现：Hal/X64/HalFpu.c + HalFpuSse.c。
 */
#include "HalFpu.h"

void HalFpuEnableThisCpu(void) {}
int HalFpuSelfTest(void) { return -1; }
int HalFpuOk(void) { return 0; }
int HalFpuBegin(void) { return 0; }
void HalFpuEnd(void) {}
