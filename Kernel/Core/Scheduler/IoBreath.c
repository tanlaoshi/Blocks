/*
 * IoBreath.c — 统一长 IO 呼吸（PR-K-preempt-breath）
 *
 * 长 Store/FAT/Present/HTTP 改调本函数；稳态仍走 YieldForPollInput。
 * 旁路 drain：Reveal 后无条件 HalInputPoll；Reveal 前极少 poll（鼠优先铁律）。
 *
 * 真机否证（NUC / bug-nuc-mouse-btn 2026-11-02）：Reveal 前 Worker 每圈
 * IoBreath 仍狂 HalInputPoll，晃鼠标占满 xHCI → iwl/静默/壁纸到不了，
 * 出光标后不跟手、墙纸缺失。与 InputTask「未露少 Poll」必须同形。
 */
#include "Scheduler.h"
#include "Hal.h"
#include "CoreOps.h"

void SchedulerIoBreath(void) {
    static UINT32 PreRevealPollDivider;
    int CursorReady;

    /*
     * 长 IO 在 Worker 上跑时 GuiTask 可自由 Poll；此处仍 drain + 光标位移，
     * 并置 NeedResched，让 CondResched 浅让出拍给 Gui（装卸期鼠标跟手）。
     * 排空键鼠会进 USB 事件环，挪光标会画界面。这两步放到开中断之后。
     * 抢占仍关掉，避免这一拍切进 Gui 再重入。
     */
    CursorReady = DesktopCursorUsbReady();
    SchedulerPreemptDisable();
    if (CursorReady || (++PreRevealPollDivider & 63u) == 0u) {
        HalInputPoll();
    }
    /* 未露：勿跟坐标/改 MousePrevBtn（半截泵鼠）；偶发 Poll 只为防环堵 */
    if (CursorReady) {
        DesktopPollMouseMotion();
    }
    SchedulerPreemptEnable();
    SchedulerSetNeedResched();
    (void)SchedulerCondResched();
}
