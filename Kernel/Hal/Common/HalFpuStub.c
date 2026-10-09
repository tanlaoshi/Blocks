/*
 * HalFpuStub.c — Arm64/RiscV：无运行时 TTF
 */
#include "HalFpu.h"

void HalFpuEnableThisCpu(void) {}
int HalFpuSelfTest(void) { return -1; }
int HalFpuOk(void) { return 0; }
int HalFpuBegin(void) { return 0; }
void HalFpuEnd(void) {}
