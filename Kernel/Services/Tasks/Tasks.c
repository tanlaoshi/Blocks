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
 * | 本文件 YieldForPollInput | Count < MIN → HalInputPoll | 稳态 drain |
 * | Hid.c TasksPumpKeyboard | Count < MIN → HalInputPoll | 同门闩 |
 * | XHCI/Irq.c InputIrqCpu | Count ≥ MIN → dest 2 else 0 | MSI 同核 |
 * | Scheduler/Start.c 钉核 | Cpus ≥ MIN → Input@2 | 与 Create 对齐 |
 * 旁路：IoBreath 无条件 drain；ConsoleOnly/PinInteractive → Input@0（F3）。
 * 现 MIN=4（1/2/4 核形态）；改数只改 Hal.h。钉核表 sched-pin-doc。
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
#include "FileSystem.h"
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
 * 稳态让步 +（按核数）drain（runtime-f-input）。
 *
 * 门槛 HAL_INPUT_CPU_MIN（现 4）：够分 0=Worker / 1=Shell·Gui / 2=Input，
 * 并按 1/2/4 核产品对齐；不是「板子必须 N 核」。Input 仍钉 2，CPU3+ 多 idle。
 *
 * 稳态 HalInputPoll 分域（禁止双路径抢 xHCI）：
 *   Count < MIN — 无 InputTask；此处 drain（Hid 同门闩）
 *   Count ≥ MIN — 仅 InputTask@CPU2 drain；此处只 pause/hlt
 * 长 Store IO：IoBreath 自带 drain（非稳态）。钉核表 sched-pin-doc。
 * 真机：勿纯 hlt 等 tick（光标锁 ~10ms+）；短自旋 + 偶发 hlt。
 */
void YieldForPollInput(void) {
    if (HalCpuCount() < HAL_INPUT_CPU_MIN) {
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
             */
            DesktopTickClock();
            for (i = 0; i < 400; i++) {
                HalCpuRelax();
            }
            (void)SchedulerCondResched();
        }
    }
}

/*
 * Worker 泵优先级（runtime-d-worker；对表用，改序另刀）：
 *   0 DesktopWorkerTick     — MSC 热插拔+钟采样（story-s3；圈首；continue 不饿死）
 *   1 DesktopEnsureIconsLoaded + MenuRebuilt — gui-icons-defer；先于旁路扫盘
 *   2 EnsureSecondary + HDA（msc/3；仍无光标）
 *   3 StoreUiEnsureOpenLoad — story-s4 开窗 defer catalog
 *   4 LocaleDbFlushStep
 *   5 LocaleTtfPreheatStep  — 可 breath；不 continue
 *   6 FontTtfWantDrain      — 同上
 *   7 HalNetInitialize（一次；boot-net-defer 有线 Probe）
 *   8 HalIwlClaim（一次）
 *   9 StoreJobStep          — story-s4；busy → continue（挡网/ready）
 *  10 HalIwlBgPump          — busy → continue
 *  11 DHCP restart / Step   — busy → continue
 *  12 真机 iwl 静默窗       — 未静默 → continue
 *  13 GuiCursorRevealAfterUsb → AnnounceBootReady
 *     （光标须在网/iwl 等慢活之后：一露就必须流畅）
 *  14 DesktopEnsureWallpaperBuilt — gui-wall-defer；Reveal 后才 Build（4K）
 *  15 halt
 * 窗/Shell 只 Enqueue；Step 在此。勿在 GuiPollMouse→StoreUiPump 里 Step。
 */
void WorkerTask(void) {
    for (;;) {
        gWorkerCount++;
        DesktopWorkerTick();
        /*
         * PR-BOOT-fast-1 / gui-icons-defer：图标/菜单在 Worker、先于 Secondary/iwl。
         * 勿放 Gui 路径读盘（与 iwl IoBreath 重入 FAT → #GP）。
         * 亦勿放 Secondary 之后：旁路 MSC 慢，BMP 会被拖成「桌面更慢」。
         */
        DesktopEnsureIconsLoaded();
        /* 切语言后扫盘重建菜单 / 落盘 lang=：必须在 Worker */
        DesktopEnsureMenuRebuilt();
        /* fs-msc-defer：桌面后慢认旁路 U 盘；光标仍等网/iwl 静默后再 Reveal */
        {
            static int sSecondaryOnce;

            if (!sSecondaryOnce) {
                FileSystemEnsureSecondaryVolumes();
                FilesUiRefresh();
                /* boot-hda-defer：旁路盘后认声卡（不挡进桌面） */
                HalHdaMmioInitialize();
                HalHdaCodecInitialize();
                HalHdaStreamInitialize();
                sSecondaryOnce = 1;
            }
        }
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
         * boot-net-defer / PR-BOOT-fast-3：桌面后先 Probe 有线，再 Claim iwl。
         * 模块表 Network 不再 HalNetInitialize，避免拖长进桌面。
         */
        {
            static int sNetOnce;
            static int sIwlClaimOnce;

            if (!sNetOnce) {
                (void)HalNetInitialize();
                sNetOnce = 1;
            }
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
        /*
         * 一露光标就必须流畅：此前 Secondary/图标/有线/iwl/DHCP 静默已过。
         * 若再提前 Reveal，空拖/点 USER 窗仍会被 Worker 慢活饿死 xHCI。
         */
        GuiCursorRevealAfterUsb();
        gConsole.AnnounceBootReady();
        (void)gConsole.RuntimeLogFlush(); /* ready 后 Log → RUNTIME.LOG */
        /*
         * gui-wall-defer：壁纸缩放在露光标之后。Build 内 IoBreath 保鼠。
         * 每圈调用；已建则早退。
         */
        DesktopEnsureWallpaperBuilt();
        HalCpuHalt();
        (void)SchedulerCondResched();
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
        if (GuiCursorUsbReady() || (++sPreCursorDiv & 15u) == 0) {
            HalIrqDisable(); /* drain 期关中断：勿在深调用中被抢 */
            HalInputPoll();
            HalIrqEnable(); /* pause 期开中断：浅栈处可被定时器抢 → ticks>0 */
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
