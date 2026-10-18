/*
 * IoBreath.c — PR-K-preempt-breath：统一长 IO 呼吸
 *
 * 长 Store/FAT/Present/HTTP 路径改调本函数；稳态仍走 YieldForPollInput。
 * 5i ❌ 曾去掉 CondResched；已恢复。5j 不改本文件。
 */
#include "Scheduler.h"
#include "Hal.h"
#include "CoreOps.h"

void SchedulerIoBreath(void) {
    /*
     * 长 IO 在 Worker 上跑时 GuiTask 可自由 Poll；此处仍 drain + 光标位移，
     * 并置 NeedResched，让 CondResched 浅让出拍给 Gui（装卸期鼠标跟手）。
     * 排空键鼠会进 USB 事件环，挪光标会画界面。这两步放到开中断之后。
     * 抢占仍关掉，避免这一拍切进 Gui 再重入。
     */
    SchedulerPreemptDisable();
    HalInputPoll();
    DesktopPollMouseMotion();
    SchedulerPreemptEnable();
    SchedulerSetNeedResched();
    (void)SchedulerCondResched();
}
