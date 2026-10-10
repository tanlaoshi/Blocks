/*
 * Scheduler.h — 调度器（K8 壳；K18 节拍；K53 Ops）
 *
 * Initialize 注册默认 SCHEDULER_OPS；Yield 经 Ops。
 */
#ifndef SCHEDULER_H
#define SCHEDULER_H

int SchedulerInitialize(void);
/* 协作让出；有节拍时睡到下一 tick */
void SchedulerYield(void);

#endif
