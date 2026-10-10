/*
 * Scheduler.c — K8 壳 + K18 LAPIC 节拍 + K53 SCHEDULER_OPS
 *
 * 【初学者】
 * 默认政策 SchedulerRoundRobinOps：开 timer 后 Yield 里 hlt 等下一拍。
 */
#include "Scheduler.h"
#include "SchedulerOps.h"
#include "HalSerial.h"
#include "HalTimer.h"
#include "SerialConfig.h"

static int gSchedReady;

static void RoundRobinInit(void) {
    gSchedReady = 0;
#if defined(__x86_64__) || defined(_M_X64)
    if (HalTimerInitialize() == 0 && HalTimerReady()) {
        HalTimerIrqEnable();
        HalSerialWriteChannel(SLOG_MISC, "Scheduler: timer ok\n");
        gSchedReady = 1;
        return;
    }
    HalSerialWriteChannel(SLOG_MISC, "Scheduler: timer skip (coop)\n");
#endif
    gSchedReady = 1;
}

static void RoundRobinYield(void) {
    if (!gSchedReady) {
        return;
    }
#if defined(__x86_64__) || defined(_M_X64)
    if (HalTimerReady()) {
        HalTimerPollLog();
        /* STI 延迟一拍：中间必须再有一条指令，否则 hlt 时 IF 仍 0 */
        __asm__ volatile("sti; nop; hlt" ::: "memory");
    } else {
        __asm__ volatile("pause");
    }
#else
    (void)0;
#endif
}

const SCHEDULER_OPS *SchedulerRoundRobinOps(void) {
    static const SCHEDULER_OPS Ops = {
        RoundRobinInit,
        RoundRobinYield,
    };
    return &Ops;
}

/*
 * SchedulerInitialize — Scheduler 模块表入口
 *
 * 做什么：注册 RoundRobinOps 并 Init（X64 尝试 HalTimerInitialize）。
 * 谁调用：ModulesRunFull（Gui 之后 Console 之前）。
 * 返回：0。
 */
int SchedulerInitialize(void) {
    SchedulerOpsRegister(SchedulerRoundRobinOps());
    if (SchedulerOpsGet() != 0 && SchedulerOpsGet()->Init != 0) {
        SchedulerOpsGet()->Init();
    }
    HalSerialWriteChannel(SLOG_MISC, "SchedulerOps: round-robin ok\n");
    return 0;
}

/*
 * SchedulerYield — 让出 CPU（协作式 / 等 LAPIC  tick）
 *
 * 做什么：调用当前 SCHEDULER_OPS->Yield。
 * 谁调用：GuiPoll、Shell 等主循环。
 */
void SchedulerYield(void) {
    const SCHEDULER_OPS *Ops = SchedulerOpsGet();
    if (Ops == 0 || Ops->Yield == 0) {
        return;
    }
    Ops->Yield();
}
