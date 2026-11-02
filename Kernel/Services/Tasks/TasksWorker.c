/*
 * TasksWorker.c — WorkerTask 泵（runtime-d-worker）
 *
 * 优先级表见函数头。与 Tasks.c（Gui/Yield/Input）拆分，单文件 ≤300。
 */
#include "Tasks.h"
#include "TasksPrivate.h"
#include "Hal.h"
#include "HalDevices.h"
#include "HalBluetooth.h"
#include "Console.h"
#include "Gui.h"
#include "Desktop.h"
#include "Locale.h"
#include "Font.h"
#include "FilesUi.h"
#include "FileSystem.h"
#include "LwIp.h"
#include "Scheduler.h"
#include "StoreJob.h"
#include "StoreUi.h"

static volatile UINT32 gWorkerCount;

UINT32 WorkerLoopCount(void) {
    return gWorkerCount;
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
 *  13 DesktopEnsureWallpaperBuilt — gui-wall-defer；静默后、Reveal 前 Build（4K）
 *     （未露时 IoBreath 少 Poll；先壁纸再露，避免「无墙纸+鼠僵」）
 *  14 GuiCursorRevealAfterUsb → AnnounceBootReady
 *     （光标须在网/iwl/壁纸之后：一露就必须流畅）
 *  15 HalBluetoothInitialize — bt-defer；Reveal 后一次（不挡进桌面）
 *  16 halt
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
         * cjk-3 / 鼠优先：光标已露且指针刚动过 → 本圈跳过 Preheat/WantDrain
         *（FT/stb 栅格会饿 xHCI）。闲时再泵；不 continue，后面网/iwl 仍走。
         */
        if (!(GuiCursorUsbReady() && !GuiPointerIdleForMs(400u))) {
            /*
             * PR-UI-ttf-3：zh 目录分片预热（每圈 1 条）。不 continue：
             * 与 iwl/DHCP 同圈交替，勿挡 Gui/鼠标。
             */
            if (LocaleTtfPreheatStep()) {
                SchedulerIoBreath();
            }
            /* 屏上 defer：每圈少抽；队列抽干才 BumpPaint → 闲时合成（防栏哆嗦） */
            if (FontTtfWantDrain(GuiCursorUsbReady() ? 8u : 16u) != 0) {
                SchedulerIoBreath();
            }
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
            /* pwifi-2 / pi-wifi-6：FS 后 Claim（只灌片）；立刻 Flush；再 Scan（Reveal 前） */
            {
                static int sWifiClaimOnce;
                if (!sWifiClaimOnce) {
                    (void)HalWifiClaim();
                    sWifiClaimOnce = 1;
                    (void)gConsole.RuntimeLogFlush();
                    if (HalWifiReady()) {
                        (void)HalWifiScan();
                        (void)gConsole.RuntimeLogFlush();
                    } else {
                        HalSerialBootMarkForce("Boot: Pi WIFI skip scan (not ready)\n");
                    }
                }
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
        LwIpDhcpOnNicEpoch(); /* 对表：epoch→自动 DHCP（与 Shell/Serial 同入口） */
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
         * gui-wall-defer：壁纸在静默后、Reveal 前。未露 IoBreath 少 Poll，
         * Build 不与跟手抢环；一露时墙纸已在（bug-nuc-mouse-btn）。
         * 每圈调用；已建则早退。
         */
        DesktopEnsureWallpaperBuilt();
        /*
         * 一露光标就必须流畅：此前 Secondary/图标/有线/iwl/DHCP/壁纸已过。
         * 若再提前 Reveal，空拖/点 USER 窗仍会被 Worker 慢活饿死 xHCI。
         */
        GuiCursorRevealAfterUsb();
        gConsole.AnnounceBootReady();
        (void)gConsole.RuntimeLogFlush(); /* ready 后 Log → RUNTIME.LOG */
        /*
         * bt-defer：桌面+光标已出再配 HCI / Reset。Hci 等待窗内仍 ServiceHid。
         * 须晚于 boot MSC claim（claim 前 Address 邻口曾假 Gone）。
         * ShellOwn 后 BootLog→RUNTIME 环（不占串口）；须再 Flush，否则只停在内存。
         */
        {
            static int sBluetoothOnce;

            if (!sBluetoothOnce) {
                (void)HalBluetoothInitialize();
                sBluetoothOnce = 1;
                (void)gConsole.RuntimeLogFlush();
                SchedulerIoBreath();
            }
        }
        HalCpuHalt();
        (void)SchedulerCondResched();
    }
}
