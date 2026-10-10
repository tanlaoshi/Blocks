/*
 * SchedulerOps.h — 可替换调度政策（K53 · Blocks 薄面）
 *
 * 【初学者】
 * 现网全量含 Enqueue/PickNext（TASK 跑队列）；Blocks 尚无任务表，
 * 本刀只钉 Init/Yield。跑队列字段后刀按现网补齐，勿在此堆第二套调度。
 */
#ifndef SCHEDULER_OPS_H
#define SCHEDULER_OPS_H

typedef struct {
    void (*Init)(void);
    void (*Yield)(void);
} SCHEDULER_OPS;

void SchedulerOpsRegister(const SCHEDULER_OPS *Ops);
const SCHEDULER_OPS *SchedulerOpsGet(void);
const SCHEDULER_OPS *SchedulerRoundRobinOps(void);

#endif
