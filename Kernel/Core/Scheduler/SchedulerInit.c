/*
 * SchedulerInit.c — InitializeScheduler（开机模块编织）
 *
 * 默认策略：同目录 SchedulerRoundRobin.c（SCHEDULER=round-robin）。
 */
#include "KernelModulesPrivate.h"
#include "Scheduler.h"

int InitializeScheduler(void) {
    SchedulerInitialize();
    return 0;
}
