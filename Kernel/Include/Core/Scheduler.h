/*
 * Scheduler.h — 调度器（K8：最小协作壳）
 *
 * 本刀只 Initialize；真抢占 / 多任务排队后刀。
 */
#ifndef SCHEDULER_H
#define SCHEDULER_H

int SchedulerInitialize(void);
/* 协作让出（本刀空操作；Console 忙等时可调用） */
void SchedulerYield(void);

#endif
