/*
 * Create.c — 内核/用户任务创建（PR-S3-sched-2）
 *
 * 从 Scheduler.c 原样搬家；不改语义。
 */
#include "Scheduler.h"
#include "SchedulerOps.h"
#include "SchedulerPrivate.h"
#include "TaskFd.h"
#include "Hal.h"
#include "VirtualMemory.h"
#include "SpinLock.h"

/* PR-GUI-kerneltask：CreateKernel 的入口。Fn/Ctx 在入队前写入槽位。 */
static void (*gKernFn[MAX_TASKS])(void *);
static void *gKernCtx[MAX_TASKS];
static void (*gPendFn)(void *);
static void *gPendCtx;

static void KernelCtxEntry(void) {
    TASK *T = CurrentTask();
    int Id = (int)(T - gTasks);
    void (*Fn)(void *) = 0;
    void *Ctx = 0;

    if (Id >= 0 && Id < MAX_TASKS) {
        Fn = gKernFn[Id];
        Ctx = gKernCtx[Id];
    }
    if (Fn) {
        Fn(Ctx);
    }
    for (;;) {
        HalCpuHalt();
    }
}

int SchedulerCreate(const char *Name, void (*Entry)(void)) {
    void (*Use)(void) = Entry;
    void (*PendFn)(void *) = 0;
    void *PendCtx = 0;

    SpinLockAcquire(&gSchedulerLock);
    if (gPendFn) {
        PendFn = gPendFn;
        PendCtx = gPendCtx;
        gPendFn = 0;
        gPendCtx = 0;
        Use = KernelCtxEntry;
    }
    for (int i = 0; i < MAX_TASKS; i++) {
        if (gTasks[i].State != TASK_UNUSED) {
            continue;
        }
        UINT8 *Top = gTasks[i].Stack + sizeof(gTasks[i].Stack);
        /*
         * 首入帧在 Stack 前 SavedFrame（非栈顶软帧）。KernelEnter 后 RSP=Top-8；
         * 首 IRQ 占 [Top-184, Top-8)。切走后 Frame 改挂 live。
         */
        HalFrameSetKernelEntry(&gTasks[i].SavedFrame, (UINT64)(UINTN)Use,
                               (UINT64)(UINTN)Top);
        gTasks[i].Frame = &gTasks[i].SavedFrame;
        gTasks[i].State = TASK_READY;
        gTasks[i].Ticks = 0;
        gTasks[i].PageRoot = VirtualMemoryKernelRoot();
        gTasks[i].IsUser = 0;
        gTasks[i].Started = 0;
        gTasks[i].UserSpace = 0;
        gTasks[i].ParentId = -1;
        gTasks[i].GroupId = -1;
        gTasks[i].LeaderId = -1;
        gTasks[i].IsThread = 0;
        gTasks[i].TlsBase = 0;
        gTasks[i].JoinerSlot = -1;
        gTasks[i].JoinTid = -1;
        gTasks[i].ExitCode = 0;
        gTasks[i].Waiting = 0;
        gTasks[i].SleepWakeTick = 0;
        gTasks[i].PendingKill = 0;
        gTasks[i].SigHandlerInt = 0;
        gTasks[i].SigHandlerTerm = 0;
        gTasks[i].Affinity = -1;
        gTasks[i].OnCpu = -1;
        gTasks[i].HomeCpu = 0;
        gTasks[i].Priority = SCHED_PRIORITY_DEFAULT;
        gTasks[i].InRunQueue = 0;
        TaskClearFds(&gTasks[i]);
        CopyName(&gTasks[i], Name);
        if (PendFn) {
            gKernFn[i] = PendFn;
            gKernCtx[i] = PendCtx;
        }
        gTaskCount++;
        {
            UINT32 Home = SchedulerOpsGet()->PickHome(&gTasks[i]);
            RunQueueEnqueue(Home, &gTasks[i]);
        }
        SpinLockRelease(&gSchedulerLock);
        return i;
    }
    SpinLockRelease(&gSchedulerLock);
    return -1;
}

int SchedulerCreateKernel(const char *Name, void (*Fn)(void *), void *Ctx) {
    if (!Name || !Fn) {
        return -1;
    }
    gPendFn = Fn;
    gPendCtx = Ctx;
    return SchedulerCreate(Name, KernelCtxEntry);
}

int SchedulerCreateUser(const char *Name, UINT64 Rip, UINT64 Rsp, UINT64 PageRoot,
                    VIRTUAL_ADDRESS_SPACE *Space, UINT64 BrkBase,
                    int StdoutSerialOnly) {
    TASK *Cur;

    SpinLockAcquire(&gSchedulerLock);
    Cur = CurrentTask();
    for (int i = 0; i < MAX_TASKS; i++) {
        if (gTasks[i].State != TASK_UNUSED) {
            continue;
        }
        UINT8 *Top = gTasks[i].Stack + sizeof(gTasks[i].Stack);
        (void)Top;
        HalFrameSetUserEntry(&gTasks[i].SavedFrame, Rip, Rsp);
        gTasks[i].Frame = &gTasks[i].SavedFrame;
        gTasks[i].State = TASK_READY;
        gTasks[i].Ticks = 0;
        gTasks[i].PageRoot = PageRoot;
        gTasks[i].IsUser = 1;
        gTasks[i].StdoutSerialOnly = StdoutSerialOnly ? 1 : 0;
        gTasks[i].Started = 0;
        gTasks[i].UserSpace = Space;
        gTasks[i].ParentId = Cur ? TaskSlot(Cur) : -1;
        gTasks[i].GroupId = (INT32)gTasks[i].Id; /* 新进程：自为组主 */
        gTasks[i].LeaderId = (INT32)gTasks[i].Id;
        gTasks[i].IsThread = 0;
        gTasks[i].TlsBase = 0;
        gTasks[i].JoinerSlot = -1;
        gTasks[i].JoinTid = -1;
        gTasks[i].ExitCode = 0;
        gTasks[i].Waiting = 0;
        gTasks[i].SleepWakeTick = 0;
        gTasks[i].PendingKill = 0;
        gTasks[i].SigHandlerInt = 0;
        gTasks[i].SigHandlerTerm = 0;
        gTasks[i].Affinity = 0; /* Console/串口非 SMP 安全；用户先钉 BSP */
        gTasks[i].OnCpu = -1;
        gTasks[i].HomeCpu = 0;
        /* 继承创建者优先级，避免 shell/gui(prio=8) 在 UP 上饿死用户(0) */
        gTasks[i].Priority = (Cur && !IsIdleTask(Cur)) ? Cur->Priority : SCHED_PRIORITY_DEFAULT;
        gTasks[i].InRunQueue = 0;
        gTasks[i].BrkBase = BrkBase;
        gTasks[i].Brk = BrkBase;
        gTasks[i].MmapNext = USER_MMAP_BASE;
        gTasks[i].Cwd[0] = 0;
        TaskClearFds(&gTasks[i]);
        CopyName(&gTasks[i], Name);
        gTaskCount++;
        /* 先不入队，EnsureTls 后再挂 READY（与 CreateThread 同） */
        SpinLockRelease(&gSchedulerLock);
        (void)SchedulerThreadEnsureTls(&gTasks[i]);
        SpinLockAcquire(&gSchedulerLock);
        if (gTasks[i].State == TASK_READY && !gTasks[i].InRunQueue) {
            UINT32 Home = SchedulerOpsGet()->PickHome(&gTasks[i]);
            RunQueueEnqueue(Home, &gTasks[i]);
        }
        SpinLockRelease(&gSchedulerLock);
        return i;
    }
    SpinLockRelease(&gSchedulerLock);
    return -1;
}
