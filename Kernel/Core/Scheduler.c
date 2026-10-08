/*
 * Scheduler.c — K8：调度最小子集
 *
 * 【初学者】
 * 完整调度会维护就绪队列、时间片、切换栈。本刀只挂上模块并标记「已就绪」，
 * 让开机表走到 Scheduler；任务切换留给后刀。
 */
#include "Scheduler.h"

static int gSchedReady;

int SchedulerInitialize(void) {
    gSchedReady = 1;
    return 0;
}

void SchedulerYield(void) {
    /* 本刀无其它任务可跑 */
    (void)gSchedReady;
}
