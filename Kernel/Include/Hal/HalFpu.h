/*
 * HalFpu.h — K27：X64 FPU/SSE 岛（仅 Font TTF 栅格用）
 *
 * 其余内核 TU 仍 -mgeneral-regs-only；栅格前 HalFpuBegin，后 HalFpuEnd。
 */
#ifndef HAL_FPU_H
#define HAL_FPU_H

#include "BootTypes.h"

void HalFpuEnableThisCpu(void);
int HalFpuSelfTest(void);
int HalFpuOk(void);
int HalFpuBegin(void);
void HalFpuEnd(void);

#endif
