/*
 * HalCpu.h — CPU 环境 HAL（K7：GDT/IDT 最小）
 *
 * X64：装内核段 + IDT（异常门指向 halt stub）；不 sti、不开定时器。
 * Arm/RiscV：成功桩。
 */
#ifndef HAL_CPU_H
#define HAL_CPU_H

/* 0=ok；非 0=失败（模块表应停） */
int HalCpuInitialize(void);

#endif
