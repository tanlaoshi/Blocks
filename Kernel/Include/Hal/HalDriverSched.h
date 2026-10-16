/*
 * HalDriverSched.h — 驱动用的调度窄钩子（不 #include Scheduler.h）
 *
 * Scheduler 模块注册；未注册时为空操作。
 */
#ifndef HAL_DRIVER_SCHED_H
#define HAL_DRIVER_SCHED_H

void HalDriverIoBreath(void);
void HalDriverPreemptDisable(void);
void HalDriverPreemptEnable(void);

void HalDriverSchedRegister(void (*IoBreath)(void),
                            void (*PreemptDisable)(void),
                            void (*PreemptEnable)(void));

#endif
