/*
 * Tasks.c — Gui / Worker / Input 三环（PR-S-tasks-1）
 *
 * SchedulerStart 后（Shell 见 Shell.c）：
 *   GuiTask    — 唯一 GuiPollMouse（Pointer.c）→ YieldForPollInput
 *   WorkerTask — DesktopWorkerTick（MSC/钟采样）→ 扫盘/Store/iwl/DHCP → ready
 *   InputTask  — 逻辑 CPU≥3：cli+HalInputPoll / sti+pause（深栈勿被抢）
 * Shell 不泵鼠标（runtime-a-poll）。YieldForPollInput：不足 3 核稳态 drain。
 * 全表 runtime-4；Gui 主环 gui-loop；钉核 sched-pin-doc；钟归属 runtime-c-tick。
 */
#include "Tasks.h"
#include "TasksPrivate.h"
#include "Hal.h"
#include "HalDevices.h"
#include "HalVideo.h"
#include "HIDKeyboard.h"
#include "Console.h"
#include "Gui.h"
#include "Desktop.h"
#include "Locale.h"
#include "Font.h"
#include "SettingsUi.h"
#include "FilesUi.h"
#include "EditUi.h"
#include "Udp.h"
#include "Tcp.h"
#include "LwIp.h"
#include "Debug.h"
#include "ShellCommands.h"
#include "SerialLog.h"
#include "Scheduler.h"
#include "StoreJob.h"
#include "StoreUi.h"

static volatile UINT32 gWorkerCount;

UINT32 WorkerLoopCount(void) {
    return gWorkerCount;
}

/*
 * 真机 poll-USB：纯 hlt 要等 PIT/HPET tick 才醒 → 光标更新锁在 ~10ms+，体感极卡。
 * 多数轮次短自旋；偶发 hlt 仍给定时器/短按电源窗口。
 */
void YieldForPollInput(void) {
    /*
     * PR-S-compose-sep（序 4）：单一 drain 主人——反例禁止「专核空转却仍在 shell
     * 同步 HalInputPoll 双路径抢设备」。故按 InputTask 是否存在分域：
     * - SMP<3（无 InputTask）：此处为稳态 drain 主人（序 1 等价）。
     * - SMP≥3（InputTask 钉 CPU2 持续 drain）：InputTask 为单一主人，此处不 drain，
     *   只让步（pause/hlt），避双路径抢 xHCI 设备。
     * 长 Store IO 不走此路径，由 StoreIoBreath 自带 drain 兜底（IO 呼吸，非稳态）。
     */
    if (HalCpuCount() < 3) {
        HalInputPoll();
    }
    if (!HalCpuIsHypervisor()) {
        UINT32 i;
        if (HalPowerButtonPressed()) {
            LogBoot("Boot: Power Button -> Shutdown\n");
            HalCpuShutdown();
        }
        /* 真机 poll-USB：勿 hlt 等 tick，否则光标锁 ~10ms+ */
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
        GuiPollMouse();
        YieldForPollInput();
    }
}

/*
 * Worker 泵优先级（runtime-d-worker；对表用，改序另刀）：
 *   0 DesktopWorkerTick     — MSC/钟采样（圈首；下表 continue 不饿死）
 *   1 DesktopEnsureIconsLoaded
 *   2 DesktopEnsureMenuRebuilt
 *   3 StoreUiEnsureOpenLoad
 *   4 LocaleDbFlushStep
 *   5 LocaleTtfPreheatStep  — 可 breath；不 continue
 *   6 FontTtfWantDrain      — 同上
 *   7 HalIwlClaim（一次）
 *   8 StoreJobStep          — busy → continue（挡网/ready）
 *   9 HalIwlBgPump          — busy → continue
 *  10 DHCP restart / Step   — busy → continue
 *  11 真机 iwl 静默窗       — 未静默 → continue
 *  12 AnnounceBootReady → halt
 * 窗/Shell 只 Enqueue；Step 在此。勿在 GuiPollMouse→StoreUiPump 里 Step。
 */
void WorkerTask(void) {
    for (;;) {
        gWorkerCount++;
        DesktopWorkerTick();
        /*
         * PR-BOOT-fast-1：图标/菜单扫盘必须在 Worker、且先于 iwl。
         * 若放 Gui 路径读盘：iwl 的 IoBreath→CondResched→Gui 再读盘会重入 FAT，
         * 真机曾见 #GP@IsrCommon iretq（rsp=0）。
         */
        DesktopEnsureIconsLoaded();
        /* 切语言后扫盘重建菜单 / 落盘 lang=：必须在 Worker */
        DesktopEnsureMenuRebuilt();
        /* 商店开窗：catalog/已装探测勿堵 Gui 点击路径 */
        StoreUiEnsureOpenLoad();
        (void)LocaleDbFlushStep();
        /*
         * PR-UI-ttf-3：zh 目录分片预热（每圈 1 条）。不 continue：
         * 与 iwl/DHCP 同圈交替，勿挡 Gui/鼠标。
         */
        if (LocaleTtfPreheatStep()) {
            SchedulerIoBreath();
        }
        /* 屏上缺字：每圈多抽几个，加快点阵→TTF */
        if (FontTtfWantDrain(16u) != 0) {
            SchedulerIoBreath();
        }
        /*
         * PR-BOOT-fast-3：桌面就绪后再 Claim iwl（BAR）；FW/关联仍走下方 BgPump。
         * FS/Net 模块内不再 Claim，避免拖长进桌面。
         */
        {
            static int sIwlClaimOnce;

            if (!sIwlClaimOnce) {
                (void)HalIwlClaim();
                sIwlClaimOnce = 1;
            }
        }
        if (StoreJobUiIsBusy()) {
            (void)StoreJobStep();
            SchedulerIoBreath();
            continue;
        }
        /* 握手先做完。中途换 L2 会把已在跑的 DHCP 停掉再要一次。 */
        if (HalIwlBgBusy()) {
            HalIwlBgPump();
            SchedulerIoBreath();
            continue;
        }
        if (!HalCpuIsHypervisor()) {
            static UINT32 DhcpEpoch;
            UINT32 Ep = HalNetNicEpoch();

            if (Ep != 0 && Ep != DhcpEpoch) {
                if (LwIpDhcpRestart(12000) == 0) {
                    DhcpEpoch = Ep;
                    LogNet("Net: dhcp queued\n");
                }
            }
        }
        if (LwIpDhcpJobBusy()) {
            (void)LwIpDhcpStep();
            SchedulerIoBreath();
            continue;
        }
        /*
         * DHCP 打完后 iwl 还可能再打一行 rx=mic（组播解密，不在 BgBusy 里）。
         * 真机再泵网卡，等到黄字静默 ~0.5s（最多 2s）再 ready。
         */
#if defined(__x86_64__)
        if (!HalCpuIsHypervisor()) {
            static UINT64 QuietArm;
            UINT32 Lo;
            UINT32 Hi;
            UINT64 Now;
            UINT64 From;
            UINT64 Last;

            __asm__ volatile("rdtsc" : "=a"(Lo), "=d"(Hi));
            Now = ((UINT64)Hi << 32) | Lo;
            if (QuietArm == 0) {
                QuietArm = Now;
            }
            HalNetPoll();
            Last = HalIwlLogTsc();
            From = QuietArm;
            if (Last > QuietArm) {
                From = Last;
            }
            /* 约 3GHz：0.5s=1.5e9，封顶 2s */
            if (Now - From < 1500000000ULL && Now - QuietArm < 6000000000ULL) {
                SchedulerIoBreath();
                continue;
            }
        }
#endif
        gConsole.AnnounceBootReady();
        (void)gConsole.RuntimeLogFlush(); /* ready 后 Log → RUNTIME.LOG */
        HalCpuHalt();
        (void)SchedulerCondResched();
    }
}

/*
 * PR-S-input-pin（序 2）：输入钉专核（SMP≥3 → 逻辑 CPU2）。
 *
 * scoped sti（关键）：sti 只在浅栈的 200-pause 期开；HalInputPoll 期 cli 关中断。
 * 原因：HalInputPoll → XchiDrainEvents → ProcessEvents* 调用链深，若被定时器在
 * 深调用中抢占，ISR 链（InterruptDispatch→SchedulerOnTimer→PickNext）压在深栈之上，
 * 实测会损坏 iret 帧（rip/rsp 变 gTasks 栈址）→ #UD/#GP。worker 之所以稳，是它只在
 * hlt（浅栈）被抢。故 InputTask 须只在浅 pause 处被抢：cli 守 drain，sti 放 pause。
 * 其专核上只有 InputTask+idle，sti 不外溢到 shell/gui 协作态（CPU1 仍 IF=0）。
 *
 * 并发安全：XchiDrainEvents 已 gEvtConsumerLock/gHidQueueLock 串行 + 命令期
 * EventIsExclusive 早退 → 与 shell/gui 的 yield-path drain 并发安全（序 1 已证）。
 */
void InputTask(void) {
    for (;;) {
        HalIrqDisable();   /* drain 期关中断：勿在深调用中被抢 */
        HalInputPoll();
        HalIrqEnable();    /* pause 期开中断：浅栈处可被定时器抢 → ticks>0 */
        {
            UINT32 i;
            for (i = 0; i < 200; i++) {
                HalCpuRelax();
            }
        }
        (void)SchedulerCondResched();
    }
}
