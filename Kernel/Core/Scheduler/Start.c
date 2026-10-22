/*
 * Start.c — SchedulerStart：idle、钉核、Online、Enter（≠ Boot/BootPkg）
 *
 * 非模块表入口。模块表只调 SchedulerInitialize（Scheduler.c）；
 * BringUp 先 Create 任务，再进本文件 SchedulerStart：
 *   每核 idle → 按名钉核 → Online → TimerStart → Enter（不返回）
 *
 * 钉核策略（只记现状；全程表路线图新 sched-pin-doc）：
 *   默认桌面：
 *     CPU0 — Worker；CPU1 — Shell+Gui（≥2）；CPU2 — Input（≥HAL_INPUT_CPU_MIN）
 *   Start 钉核用 HAL_INPUT_CPU_MIN（F2）；任务谁 Create：Kernel.c。
 *
 * ── ConsoleOnly / Pin vs Input 门槛（input-f3-console；勿与 F2 缠改）──
 *
 * HalConsoleOnly（选路，KernelMain）：
 *   真 → Virt 模块表 + KernelMainVirtEnterShell：只 Create Shell
 *        （不 Create Gui/Worker/Input；门槛 MIN 无对象）
 *   假 → Common：Shell/Gui/Worker + 可选 Input
 *
 * HalPinInteractiveToBootstrap（钉核例外，本文件 SchedulerStart）：
 *   与 ConsoleOnly 任一为真 → InteractiveCpu=InputCpu=0（盖过 MIN→CPU2）
 *   用途：定时器只在 BSP 时，交互钉 AP 会永 WFI
 *   现三架构均返回 0；活的串口子集主要靠 ConsoleOnly（arm/riscv 无 FB）
 *
 * x64：ConsoleOnly/Pin 恒 0 → 课堂桌面永不进上述例外。
 */
#include "Scheduler.h"
#include "SchedulerOps.h"
#include "SchedulerPrivate.h"
#include "TaskFd.h"
#include "Syscall.h"
#include "Hal.h"
#include "Debug.h"
#include "VirtualMemory.h"
#include "PhysicalMemory.h"
#include "SpinLock.h"
#include "SerialLog.h"

static int NameStarts(const char *Name, char A, char B) {
    char C0;
    char C1;

    if (!Name) {
        return 0;
    }
    C0 = Name[0];
    C1 = Name[1];
    if (C0 >= 'A' && C0 <= 'Z') {
        C0 = (char)(C0 - 'A' + 'a');
    }
    if (C1 >= 'A' && C1 <= 'Z') {
        C1 = (char)(C1 - 'A' + 'a');
    }
    return C0 == A && C1 == B;
}

static int CreateIdleForCpu(UINT32 Cpu) {
    char Name[12];
    int Id;

    Name[0] = 'I';
    Name[1] = 'd';
    Name[2] = 'l';
    Name[3] = 'e';
    Name[4] = (char)('0' + (Cpu % 10));
    Name[5] = 0;
    Id = SchedulerCreate(Name, IdleTask);
    if (Id < 0) {
        return -1;
    }
    SchedulerSetAffinity(Id, (INT32)Cpu);
    SpinLockAcquire(&gSchedulerLock);
    gIdleSlot[Cpu] = Id;
    gTasks[Id].Priority = SCHED_PRIORITY_IDLE;
    SchedulerOpsGet()->Remove(&gTasks[Id]);
    SpinLockRelease(&gSchedulerLock);
    return Id;
}

int SchedulerIsOnline(void) {
    return gSchedulerOnline;
}

void SchedulerApStart(void) {
    UINT32 Cpu;
    TASK *Idle;
    HAL_INTERRUPT_FRAME *Frame;

    Cpu = HalCpuGetId();
    while (!gSchedulerOnline) {
        HalCpuRelax();
    }
    SpinLockAcquire(&gSchedulerLock);
    Idle = IdleTaskForCpu(Cpu);
    if (!Idle) {
        SpinLockRelease(&gSchedulerLock);
        LogSmp("Boot: Scheduler AP Has No Idle\n");
        for (;;) {
            HalCpuPark();
        }
    }
    /*
     * SmpBoot 在进本函数前已 sti。首入前 cli；Frame 须为 SavedFrame（栈外），
     * 勿指 IRQ 栈槽（见 OnTimer Capture / 历史 SP=0 #PF）。
     */
    HalIrqDisable();
    Frame = Idle->Frame;
    if (!Frame || Frame != &Idle->SavedFrame ||
        HalFrameGetStackPointer(Frame) < 0x10000ULL) {
        UINT8 *Top = Idle->Stack + sizeof(Idle->Stack);
        Frame = &Idle->SavedFrame;
        HalFrameSetKernelEntry(Frame, (UINT64)(UINTN)IdleTask,
                               (UINT64)(UINTN)Top);
        Idle->Frame = Frame;
        LogSmp("Boot: Scheduler AP Repaired Idle Frame\n");
    }
    ActivateTask(Idle);
    Idle->Started = 1;
    SpinLockRelease(&gSchedulerLock);
    /* 8 AP 并发写 COM1 会把欢迎语打成乱码；只留一条样例给冒烟。
     * 与 Worker 上 iwl bg=* 交错属正常：SchedulerStart 后 AP 与 Worker 并行。 */
    if (Cpu == 1) {
        LogSmp("Boot: Scheduler AP Entered Idle Cpu=");
        LogSmpHex32(Cpu);
        LogSmp("\n");
    }
    HalSchedulerEnter(Frame);
    for (;;) {
        HalCpuPark();
    }
}

void SchedulerStart(void) {
    TASK *First = 0;
    int Cpus;
    int c;
    int i;

    Cpus = HalCpuCount();
    if (Cpus < 1) {
        Cpus = 1;
    }
    if (Cpus > HAL_MAX_CPUS) {
        Cpus = HAL_MAX_CPUS;
    }
    for (c = 0; c < Cpus; c++) {
        if (CreateIdleForCpu((UINT32)c) < 0) {
            LogMisc("Boot: Scheduler Idle Create Failed\n");
            for (;;) {
                HalCpuPark();
            }
        }
    }

    /* 钉核：表与例外见文件头 / 路线图新 sched-pin-doc（此处只执行，不改策略）。 */
    {
        UINT32 InteractiveCpu = (Cpus > 1) ? 1u : 0u;
        UINT32 InputCpu = (Cpus >= HAL_INPUT_CPU_MIN) ? 2u : InteractiveCpu;

        if (HalConsoleOnly() || HalPinInteractiveToBootstrap()) {
            InteractiveCpu = 0;
            InputCpu = 0;
        }

        for (i = 0; i < MAX_TASKS; i++) {
            if (gTasks[i].State == TASK_UNUSED) {
                continue;
            }
            if (NameStarts(gTasks[i].Name, 'i', 'n')) {
                SchedulerOpsGet()->Remove(&gTasks[i]);
                gTasks[i].Affinity = (INT32)InputCpu;
                gTasks[i].HomeCpu = (INT32)InputCpu;
                gTasks[i].Priority = SCHED_PRIORITY_DEFAULT;
                RunQueueEnqueue(InputCpu, &gTasks[i]);
                continue;
            }
            if (NameStarts(gTasks[i].Name, 's', 'h') ||
                NameStarts(gTasks[i].Name, 'g', 'u')) {
                SchedulerOpsGet()->Remove(&gTasks[i]);
                gTasks[i].Affinity = (INT32)InteractiveCpu;
                gTasks[i].HomeCpu = (INT32)InteractiveCpu;
                gTasks[i].Priority = SCHED_PRIORITY_SHELL;
                RunQueueEnqueue(InteractiveCpu, &gTasks[i]);
                continue;
            }
            if (NameStarts(gTasks[i].Name, 'w', 'o')) {
                SchedulerOpsGet()->Remove(&gTasks[i]);
                gTasks[i].Affinity = 0;
                gTasks[i].HomeCpu = 0;
                /* UP：与 shell 同核时提到 SHELL，否则 DEFAULT 会被饿死 */
                gTasks[i].Priority = (Cpus <= 1) ? SCHED_PRIORITY_SHELL
                                                 : SCHED_PRIORITY_DEFAULT;
                RunQueueEnqueue(0, &gTasks[i]);
            }
        }
    }

    First = 0;
    {
        TASK *Idle0 = IdleTaskForCpu(0);

        for (i = 0; i < MAX_TASKS; i++) {
            if (gTasks[i].State != TASK_READY) {
                continue;
            }
            if (Idle0 && &gTasks[i] == Idle0) {
                continue;
            }
            /* BSP 勿直接切入钉在 AP 上的任务 */
            if (gTasks[i].Affinity >= 0 && gTasks[i].Affinity != 0) {
                continue;
            }
            First = &gTasks[i];
            break;
        }
    }
    if (!First) {
        First = IdleTaskForCpu(0);
    }
    if (!First) {
        LogMisc("Boot: Scheduler No Tasks\n");
        for (;;) {
            HalCpuPark();
        }
    }

    HalIrqDisable();
    SpinLockAcquire(&gSchedulerLock);
    ActivateTask(First);
    First->Started = 1;
    gSchedulerOnline = 1;
    {
        HAL_INTERRUPT_FRAME *Frame = First->Frame;

        SpinLockRelease(&gSchedulerLock);
        HalTimerStart();
        DebugWrite("Boot: Scheduler Online\n");
        /*
         * 保持 cli 直到 KernelEnter→iretq。Frame.Rflags 已带 IF=1，iret 后开中断。
         * 若在此 HalIrqEnable，定时器可能在仍占 gEarlyStack 时入 Shell（Started=1），
         * live 帧 SP∈early → sched: corrupt resume（CI/本地冒烟稳复现）。
         * AP 首入仍走 Idle→Halt 开中断路径。
         */
        HalSchedulerEnter(Frame);
    }
}

TASK *SchedulerCurrent(void) {
    return CurrentTask();
}

int SchedulerTaskCount(void) {
    return gTaskCount;
}


const TASK *SchedulerTaskByIndex(int Index) {
    if (Index < 0 || Index >= MAX_TASKS) {
        return 0;
    }
    if (gTasks[Index].State == TASK_UNUSED) {
        return 0;
    }
    return &gTasks[Index];
}

UINT64 SchedulerTaskRip(const TASK *T) {
    if (!T || !T->Frame) {
        return 0;
    }
    return HalFrameGetInstructionPointer(T->Frame);
}
