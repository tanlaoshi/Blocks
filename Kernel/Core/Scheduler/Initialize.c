/*
 * Initialize.c — InitializeScheduler（开机模块编织）
 *
 * 默认策略：同目录 RoundRobin.c（SCHEDULER=round-robin）。
 */
#include "KernelModulesPrivate.h"
#include "Scheduler.h"
#include "HalDriverSched.h"

int InitializeScheduler(void) {
    SchedulerInitialize();
    HalDriverSchedRegister(SchedulerIoBreath, SchedulerPreemptDisable,
                           SchedulerPreemptEnable);
    return 0;
}
