/*
 * Scheduler.h — 调度器（K8 壳；K18 节拍）
 *
 * Initialize 可开 LAPIC timer；Yield 在有 timer 时 hlt 等待下一拍。
 */
#ifndef SCHEDULER_H
#define SCHEDULER_H

int SchedulerInitialize(void);
/* 协作让出；有节拍时睡到下一 tick */
void SchedulerYield(void);

#endif
