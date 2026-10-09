/*
 * HalCpu.h — CPU 环境 HAL（K7 GDT/IDT；K18 可改门）
 *
 * X64：装内核段 + IDT；默认不 sti。
 * Arm/RiscV：成功桩。
 */
#ifndef HAL_CPU_H
#define HAL_CPU_H

#include "BootTypes.h"

/* 0=ok；非 0=失败（模块表应停） */
int HalCpuInitialize(void);
/* K18：给定时器等挂处理函数（Vec 建议 ≥32） */
void HalCpuIdtSet(UINT32 Vec, void *Handler);

#endif
