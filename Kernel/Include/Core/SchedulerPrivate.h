/*
 * SchedulerPrivate.h — 调度器内部（仅 Core；User 勿 include）
 *
 * PR-S-sched-split-1：runq
 * PR-S-sched-split-2：wait/exit 共享符号
 */
#ifndef SCHEDULER_PRIVATE_H
#define SCHEDULER_PRIVATE_H

#include "Scheduler.h"
#include "CString.h"
#include "SpinLock.h"

/* 定义在 Scheduler.c */
extern TASK gTasks[MAX_TASKS];
extern int gTaskCount;
extern INT32 gIdleSlot[HAL_MAX_CPUS]; /* 5k：槽位；-1=无 · 勿存裸 TASK* */
extern SPIN_LOCK gSchedulerLock;
extern int gCoopDrain;
extern volatile int gSchedulerOnline;

#define SIG_HANDLER_DFL 0ULL
#define SIG_HANDLER_IGN 1ULL

void CopyName(TASK *T, const char *Name);
void IdleTask(void);
TASK *IdleTaskForCpu(UINT32 Cpu);
TASK *FindRunnable(UINT32 Cpu);
int SignalDefaultTerminates(INT32 Sig);
UINT64 *SignalHandlerSlot(TASK *T, INT32 Sig);
UINT64 SignalHandlerGet(TASK *T, INT32 Sig);
int DeliverToHandlerFrame(TASK *T, HAL_INTERRUPT_FRAME *F, UINT64 Handler, INT32 Sig);
int DeliverKillLocked(TASK *T, INT32 Sig, int *ShowPrompt,
                      VIRTUAL_ADDRESS_SPACE **OutSpace,
                      HAL_INTERRUPT_FRAME *LiveFrame);

TASK *CurrentTask(void);
void SetCurrentTask(TASK *T);
int IsIdleTask(const TASK *T);
INT32 TaskSlot(const TASK *T);
void ActivateTask(TASK *T);
UINT64 SchedulerResumeFrame(TASK *T);

/*
 * 切走/阻塞：挂上本拍 live 帧（在任务栈上，冻结期间有效）。
 * 勿 HalFrameCopy 到 SavedFrame 再 mov rsp 过去——与历史 IsrCommon 语义不一致
 * 曾致 #GP rip∈Stack rsp=垃圾。
 */
static inline void SchedulerPinLiveFrame(TASK *T, HAL_INTERRUPT_FRAME *Live) {
    if (T && Live) {
        T->Frame = Live;
    }
}

/* PR-K-preempt-needresched：OnTimer 置位；CondResched 见 Scheduler.h */
void SchedulerSetNeedResched(void);

/* 定义在 Runq.c */
void RunQueueInitialize(void);
void RunQueueEnqueue(UINT32 Cpu, TASK *T);
void RunQueueRemove(TASK *T);
TASK *PickNext(UINT32 Cpu);
void SchedulerRunQueueView(UINT32 Cpu, TASK ***Slots, int **Count);

/* 定义在 Terminate.c（thr-3 自 Wait 拆出） */
int TerminateUserLocked(TASK *Exiting, INT32 Code, int *ShowPrompt,
                        VIRTUAL_ADDRESS_SPACE **OutSpace);
void SchedulerDestroyDetached(VIRTUAL_ADDRESS_SPACE *Space);
void SchedulerReapZombie(TASK *Z); /* join / wait 收尸 */

#endif
