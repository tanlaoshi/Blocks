/*
 * SchedulerOps.c — SCHEDULER_OPS 注册（K53）
 *
 * 【初学者】
 * - Core/Scheduler：政策表指针；默认 RoundRobin 在 Scheduler.c 注册。
 * - 入口：SchedulerOpsRegister / SchedulerOpsGet。
 * - 边界：不直接开 LAPIC；Timer 在 HalTimer*。
 */
#include "SchedulerOps.h"

static const SCHEDULER_OPS *gOps;

/*
 * SchedulerOpsRegister — 安装调度政策（RoundRobin 等）
 *
 * 做什么：保存 SCHEDULER_OPS 指针。
 * 谁调用：SchedulerInitialize。
 */
void SchedulerOpsRegister(const SCHEDULER_OPS *Ops) {
    gOps = Ops;
}

/*
 * SchedulerOpsGet — 当前调度政策
 *
 * 做什么：返回 gOps；SchedulerYield 经此调用 Yield。
 * 谁调用：SchedulerInitialize / SchedulerYield。
 */
const SCHEDULER_OPS *SchedulerOpsGet(void) {
    return gOps;
}
