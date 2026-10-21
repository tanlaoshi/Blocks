/*
 * Wait.c — PR-S-sched-split-2：wait / orphan / exit 用户入口
 *
 * 收尸与 TerminateUserLocked → Terminate.c（thr-3 拆分）。
 * S2.2：ExitUser 仅在确有关掉 USER 窗时 DesktopComposeThemeScene（Hello 无窗勿全桌合成）。
 * S2.3：ExitUser 切回 KernelRoot 只 LoadPageTable 一次（曾连续两次）。
 */
#include "SchedulerPrivate.h"
#include "SchedulerOps.h"
#include "Syscall.h"
#include "Hal.h"
#include "SerialLog.h"
#include "CoreOps.h"
#include "Debug.h"
#include "VirtualMemory.h"
#include "ProcessPrivate.h"

void SchedulerReapOrphanZombies(void) {
    int i;

    SpinLockAcquire(&gSchedulerLock);
    for (i = 0; i < MAX_TASKS; i++) {
        if (gTasks[i].State != TASK_ZOMBIE || !gTasks[i].IsUser) {
            continue;
        }
        if (gTasks[i].IsThread) {
            continue; /* 等 join；勿当孤儿进程尸 */
        }
        {
            TASK *P;
            INT32 ParentSlot = gTasks[i].ParentId;
            int UserParent = 0;

            if (ParentSlot >= 0 && ParentSlot < MAX_TASKS) {
                P = &gTasks[ParentSlot];
                if (P->State != TASK_UNUSED && P->IsUser) {
                    UserParent = 1;
                }
            }
            if (!UserParent) {
                SchedulerReapZombie(&gTasks[i]);
            }
        }
    }
    SpinLockRelease(&gSchedulerLock);
}

int SchedulerLiveUserApps(void) {
    int i;

    for (i = 0; i < MAX_TASKS; i++) {
        if (!gTasks[i].IsUser) {
            continue;
        }
        if (gTasks[i].State == TASK_UNUSED || gTasks[i].State == TASK_ZOMBIE) {
            continue;
        }
        return 1;
    }
    return 0;
}

UINT64 SchedulerExitUser(HAL_INTERRUPT_FRAME *Frame) {
    INT32 Code;
    TASK *Exiting;
    TASK *Next;
    UINT32 Cpu;
    UINT64 Ret;
    int ShowPrompt = 0;
    VIRTUAL_ADDRESS_SPACE *Detached = 0;

    SpinLockAcquire(&gSchedulerLock);
    Exiting = CurrentTask();
    if (Exiting == 0 || !Exiting->IsUser) {
        SpinLockRelease(&gSchedulerLock);
        for (;;) {
            HalCpuPark();
        }
    }

    Code = (INT32)HalFrameGetArgument0(Frame);
    DebugWrite("syscall: exit ");
    DebugWrite(Exiting->Name);
    DebugWrite(" code=");
    DebugHex32((UINT32)Code);
    DebugWrite("\n");

    /*
     * 非最后用户则勿 CloseAllUserWindows：短命 ELF exit 会拆掉仍在跑的 GUI 窗。
     */
    {
        int J;
        int LastUser = 1;
        int ClosedUser = 0;

        for (J = 0; J < MAX_TASKS; J++) {
            TASK *U = &gTasks[J];

            if (U != Exiting && U->IsUser &&
                (U->State == TASK_READY || U->State == TASK_RUNNING ||
                 U->State == TASK_BLOCKED)) {
                LastUser = 0;
                break;
            }
        }
        (void)TerminateUserLocked(Exiting, Code, &ShowPrompt, &Detached);
        SpinLockRelease(&gSchedulerLock);
        VirtualMemoryLoadPageTable(VirtualMemoryKernelRoot());
        DesktopWaitNoWindowClosing();
        if (LastUser) {
            ClosedUser = DesktopCloseAllUserWindows();
            DesktopWaitNoWindowClosing();
            ProcessRestoreAppFont();
        }
        /*
         * USER 轻量关窗后补全桌合成（任务栏/残影）。
         * Hello 等从未开 USER 窗：ClosedUser=0，勿 ThemeScene（S2.2）。
         */
        if (ClosedUser > 0) {
            DesktopComposeThemeScene();
        }
    }
    SchedulerDestroyDetached(Detached);
    Detached = 0;
    SpinLockAcquire(&gSchedulerLock);

    if (gCoopDrain) {
        SpinLockRelease(&gSchedulerLock);
        /*
         * PR-V-ap-interactive：CoopDrain 在 shell 的 gConsole.OnEnter 内同步跑完；
         * 此处勿 ShowPrompt，否则与末尾 ConsolePromptAfterCommand 叠成双「blocks>」。
         * x86 异步收尸仍走下方 ShowPrompt。
         */
        HalUserCoopReturn();
        return 0;
    }

    Cpu = HalCpuGetId();
    Next = SchedulerOpsGet()->PickNext(Cpu);
    if (!Next) {
        SpinLockRelease(&gSchedulerLock);
        LogMisc("sched: no runnable task after exit\n");
        for (;;) {
            HalCpuPark();
        }
    }
    ActivateTask(Next);
    Ret = SchedulerResumeFrame(Next);
    SpinLockRelease(&gSchedulerLock);
    if (ShowPrompt) {
        ConsoleIoShowPrompt();
    }
    return Ret;
}

UINT64 SchedulerWait(HAL_INTERRUPT_FRAME *Frame) {
    TASK *Self;
    INT32 MyId;
    int i;
    int Live = 0;
    TASK *Next;
    UINT64 Options;
    UINT32 Cpu;
    UINT64 Ret;

    SpinLockAcquire(&gSchedulerLock);
    Self = CurrentTask();
    if (Self == 0 || !Self->IsUser) {
        HalFrameSetReturn(Frame, (UINT64)(INT64)-1);
        SpinLockRelease(&gSchedulerLock);
        return 0;
    }
    MyId = TaskSlot(Self);
    Options = HalFrameGetArgument0(Frame);

    for (i = 0; i < MAX_TASKS; i++) {
        if (gTasks[i].State == TASK_ZOMBIE && gTasks[i].ParentId == MyId &&
            !gTasks[i].IsThread) {
            INT32 Code = gTasks[i].ExitCode;
            INT32 Cid = TaskSlot(&gTasks[i]);
            SchedulerReapZombie(&gTasks[i]);
            HalFrameSetReturn2(Frame, (UINT64)(UINT32)Cid, (UINT64)(UINT32)Code);
            SpinLockRelease(&gSchedulerLock);
            return 0;
        }
    }
    for (i = 0; i < MAX_TASKS; i++) {
        if (gTasks[i].ParentId == MyId && !gTasks[i].IsThread &&
            (gTasks[i].State == TASK_READY ||
             gTasks[i].State == TASK_RUNNING ||
             gTasks[i].State == TASK_BLOCKED)) {
            Live = 1;
            break;
        }
    }
    if (!Live) {
        HalFrameSetReturn(Frame, (UINT64)(INT64)-1);
        SpinLockRelease(&gSchedulerLock);
        return 0;
    }

    if (Options & (UINT64)WNOHANG) {
        HalFrameSetReturn2(Frame, 0, 0);
        SpinLockRelease(&gSchedulerLock);
        return 0;
    }

    SchedulerPinLiveFrame(Self, Frame);
    Self->Waiting = 1;
    Self->State = TASK_BLOCKED;
    Self->OnCpu = -1;

    Cpu = HalCpuGetId();
    Next = SchedulerOpsGet()->PickNext(Cpu);
    if (!Next) {
        SpinLockRelease(&gSchedulerLock);
        LogMisc("sched: wait with no runnable task\n");
        for (;;) {
            HalCpuPark();
        }
    }
    ActivateTask(Next);
    Ret = SchedulerResumeFrame(Next);
    SpinLockRelease(&gSchedulerLock);
    return Ret;
}
