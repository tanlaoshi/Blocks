/*
 * Tasks.c — Gui / Worker / Input 三环（input-f1-gates 清单主表）
 *
 * SchedulerStart 后（Shell 见 Shell.c）：
 *   GuiTask    — 唯一 GuiPollMouse（Pointer.c）→ YieldForPollInput
 *   WorkerTask — DesktopWorkerTick（含 MSC 热插拔 story-s3）→ 扫盘/Store/iwl/DHCP → ready
 *   InputTask  — Count≥HAL_INPUT_CPU_MIN 才 Create，钉 CPU2（Hal.h）
 * Shell 不泵鼠标（runtime-a-poll）。钉核大表 sched-pin-doc；四环 runtime-4。
 *
 * ── Input 门槛门闩（F1 清单 + F2 同一常数 HAL_INPUT_CPU_MIN）──
 *
 * | 点 | 判定 | 作用 |
 * | Kernel.c KernelTrySpawnInput | Count < MIN → return | 不 Create |
 * | 本文件 YieldForPollInput | Count < MIN → HalInputPoll；!TimerIrq → 鼠+µSD 失根探 | 稳态 drain |
 * | Hid.c TasksPumpKeyboard | Count < MIN → HalInputPoll | 同门闩 |
 * | XHCI/Irq.c InputIrqCpu | Count ≥ MIN → dest 2 else 0 | MSI 同核 |
 * | Scheduler/Start.c 钉核 | Cpus ≥ MIN → Input@2 | 与 Create 对齐 |
 * 旁路：IoBreath Reveal 后无条件 drain、未露少 Poll；ConsoleOnly → Input@0（F3）。
 * WorkerTask 见 TasksWorker.c。现 MIN=4；改数只改 Hal.h。钉核表 sched-pin-doc。
 */
#include "Tasks.h"
#include "TasksPrivate.h"
#include "Hal.h"
#include "HalPlatform.h"
#include "HalConsole.h"
#include "HalDevices.h"
#include "Gui.h"
#include "Desktop.h"
#include "Scheduler.h"
#include "SerialLog.h"

/*
 * 无 Input 专核时调用方自 drain（对表 KernelTrySpawnInput）。
 * Count<MIN（Duo 单核等）与 Pi VirtDesktop 同枝；有专核（NUC）返回 0。
 */
int TasksCallerDrainsInput(void) {
    if (HalCpuCount() < HAL_INPUT_CPU_MIN) {
        return 1;
    }
    if (!HalCpuIsHypervisor() && HalPlatformUseVirtDesktop()) {
        return 1;
    }
    return 0;
}

static int TasksInputDrainHere(void) {
    return TasksCallerDrainsInput();
}

/*
 * 稳态让步 +（按核数）drain（runtime-f-input）。
 *
 * 门槛 HAL_INPUT_CPU_MIN（现 4）：够分 0=Worker / 1=Shell·Gui / 2=Input，
 * 并按 1/2/4 核产品对齐；不是「板子必须 N 核」。Input 仍钉 2，CPU3+ 多 idle。
 *
 * 稳态 HalInputPoll 分域（禁止双路径抢 xHCI；Pi DWC2 例外见下）：
 *   Count < MIN — 无 InputTask；此处 drain（Hid 同门闩）
 *   Count ≥ MIN — 仅 InputTask@CPU2 drain；此处只 pause/hlt
 *   Pi VirtDesktop — 不 Create Input；此处仍 drain（单 DWC2，避跨核并触）
 * 长 Store IO：IoBreath 自带 drain（非稳态）。钉核表 sched-pin-doc。
 * 真机：勿纯 hlt 等 tick（光标锁 ~10ms+）；短自旋 + 偶发 hlt。
 */
void YieldForPollInput(void) {
    if (TasksInputDrainHere()) {
        HalInputPoll();
        /*
         * Pi CNTV-poll：无定时器 IRQ 时 CondResched 无法切到 GuiTask，
         * Shell 环须代泵鼠。
         * psmp 后 UsesIrq=1：Gui 与 Shell 同核（Pin BSP）时仍易被
         * HalNetPoll/LwIp 长圈饿死 → 光标不跟、TickClock 不跑、托盘红
         * 而壳 `ip` 有址（2026-11-08）。Pi VirtDesktop 有 IRQ 也代泵。
         * x64/virt：仍只 GuiTask 泵（!UsesIrq 才进此枝的旧条件保留给非 Pi）。
         */
        if (GuiCursorUsbReady()) {
            if (!HalTimerUsesIrq() ||
                (!HalCpuIsHypervisor() && HalPlatformUseVirtDesktop())) {
                GuiPollMouse();
            }
        }
    }
    /*
     * Pi µSD 失根：Notify→大窗在 PollUnplug 内。
     * 无 IRQ 时 Worker 易饿 → 须 Shell 代探。
     * 有 IRQ 后若仍只挂 !UsesIrq：Pin BSP 下 Shell 长圈网泵仍饿 Worker →
     * 拔卡无提示（2026-11-08）。Pi VirtDesktop 有 IRQ 也代探。
     */
    if (!HalTimerUsesIrq() ||
        (!HalCpuIsHypervisor() && HalPlatformUseVirtDesktop())) {
        (void)HalUsbMscPollUnplug();
    }
    if (!HalCpuIsHypervisor()) {
        UINT32 i;
        if (HalPowerButtonPressed()) {
            HalConsoleWriteSerial("Boot: Power Button -> Shutdown\n");
            HalCpuShutdown();
        }
        /* 真机 poll-USB：勿 hlt 等 tick，否则光标锁 ~10ms+ */
        HalTimerPoll();
        for (i = 0; i < 200; i++) {
            HalCpuRelax();
        }
        (void)SchedulerCondResched();
        return;
    }
    HalCpuHalt();
    (void)SchedulerCondResched();
}

void GuiTask(void) {
    for (;;) {
        /*
         * 光标未 Reveal：勿泵鼠。晃鼠标会占满 xHCI/本核，Worker 到不了
         * iwl/DHCP/Reveal → 光标永不出现、网托一直红。
         */
        if (GuiCursorUsbReady()) {
            GuiPollMouse();
            YieldForPollInput();
        } else {
            UINT32 i;

            /*
             * 勿泵鼠；仍跑 TickClock：Worker 已 Ensure 的图标 NeedRefresh
             * 须在 Reveal 前 Consume，否则 BMP 要等网/iwl 完才出（观感更慢）。
             * Pi !TimerIrq：未露也须 HalTimerPoll，否则 tick 不走、网托 5s
             * 采样卡死、有 ip 仍一直红（2026-11-04）。
             */
            if (!HalTimerUsesIrq()) {
                HalTimerPoll();
            }
            DesktopTickClock();
            if (!HalCpuIsHypervisor() && HalPowerButtonPressed()) {
                HalConsoleWriteSerial("Boot: Power Button -> Shutdown\n");
                HalCpuShutdown();
            }
            for (i = 0; i < 400; i++) {
                HalCpuRelax();
            }
            (void)SchedulerCondResched();
        }
    }
}

/*
 * Input 专核（Count≥HAL_INPUT_CPU_MIN；Create 见 KernelTrySpawnInput）。
 * 稳态唯一 HalInputPoll 主人；门槛/分域见 Yield 头；钉核 sched-pin-doc。
 *
 * scoped sti：drain 期 cli（深调用勿被抢 → iret 帧坏 → #UD/#GP）；
 * pause 期 sti（浅栈可被定时器抢）。专核仅 Input+idle；CPU1 仍 IF=0。
 */
void InputTask(void) {
    static UINT32 sPreCursorDiv;

    for (;;) {
        /*
         * Reveal 前少 Poll：仍偶发 drain 防环堵，但不让晃鼠标饿死 Worker。
         */
        /* &63：与 IoBreath 未露节流同量级；过密仍会饿 Worker（NUC 真机） */
        if (GuiCursorUsbReady() || (++sPreCursorDiv & 63u) == 0) {
            HalIrqDisable(); /* drain 期关中断：勿在深调用中被抢 */
            HalInputPoll();
            HalIrqEnable(); /* pause 期开中断：浅栈处可被定时器抢 → ticks>0 */
        }
        if (!HalCpuIsHypervisor() && HalPowerButtonPressed()) {
            HalConsoleWriteSerial("Boot: Power Button -> Shutdown\n");
            HalCpuShutdown();
        }
        {
            UINT32 i;
            for (i = 0; i < 200; i++) {
                HalCpuRelax();
            }
        }
        (void)SchedulerCondResched();
    }
}
