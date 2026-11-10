/*
 * Clock.c — 任务栏钟 / MSC 热插拔收口（usb-map / story-s3 / runtime-c-tick）
 *
 * ```
 * WorkerTask 圈首 DesktopWorkerTick ~2Hz
 *   → HalUsbMscPollUnplug（指针忙也跑；失根大窗）
 *   → 指针忙则 return（热插 Scan / CMOS 让路保鼠）
 *   → HalUsbMscHotPoll（热插；拔已在 PollUnplug）
 *   → RTC/网托 ~5s（勿 2Hz CMOS：SMM 会令空拖规律顿）
 *   → BootMediumGone? 大窗 → HalCpuShutdown（不 Remount，勿扶旁路）
 * ```
 * 旁路 U 盘热拔：不关机。启动盘失根（X64 Live USB / Pi µSD）：Gone → 大窗关机。
 * 钟/网托→栏脏；Present 仍 DesktopTickClock（指针闲才画栏）。勿 Worker 上全量 Compose。
 */
#include "DesktopPrivate.h"
#include "Gui.h" /* GuiDragActive；指针闲 */
#include "GuiPrivate.h" /* ComposeBeginEraseCursor；勿 GuiCursorHide（Hw 会 Disable=闪） */
#include "FilesUi.h"
#include "FileSystem.h"
#include "Font.h"
#include "SerialLog.h"
#include "Hal.h"
#include "HalVideo.h"

static volatile int sClockBarNeedPaint;

void DesktopRequestTaskbarRepaint(void) {
    /* 只抬钟栏脏；闲时 TickClock 铺底+画栏。勿 RequestRefresh→ThemeScene 叠半透。 */
    sClockBarNeedPaint = 1;
}

void DesktopNotifyNetChanged(void) {
    /*
     * 与 Worker 网托采样同终点：Gui TickClock 闲时 DrawTaskbar。
     * 旧路径再 RequestRefresh：Compose 画栏后又 TickClock 再 Present → 偶发哆嗦
     * （bug-nuc-taskbar-flicker）。托盘弹出时仍走 Refresh 快路径重画 popup。
     */
    DesktopRequestTaskbarRepaint();
    if (DesktopNetTrayIsOpen()) {
        DesktopMarkNetTrayPopup();
        RequestRefresh();
    }
}

void DesktopWorkerTick(void) {
    static UINT64 LastCheckTick;
    static UINT64 LastRtcNetTick;
    static int sRemountWant;
    UINT64 Now;
    UINT32 Tps;
    UINT8 Hour = 0;
    UINT8 Minute = 0;
    int Ok;
    int NeedPaint;
    int Hot;

    /*
     * 节流必须用 HalCpuTicks，勿改 HalMilliseconds。
     * Arm64 CNTV IRQ 默认 10ms → 软 tick≈100Hz，而 HalTicksPerSec()=1000：
     * 名不副实。绿基线靠这套「偏慢」节流；改成真毫秒后 HotPoll≈0.5s 一拍，
     * Pi 单控 DWC2 闲置十余分钟会饿死 HID（2026-11-08）。
     * 墙钟本身仍走 SoftRtc（HalRtcGetTime/CNTVCT）。
     */
    Now = HalCpuTicks(0);
    Tps = HalTicksPerSec();
    if (Tps == 0) {
        Tps = 250;
    }
    /* HotPoll 入口仍 ~2Hz（名义；实墙钟更疏，见上） */
    if (LastCheckTick != 0 && (Now - LastCheckTick) < (UINT64)(Tps / 2u)) {
        return;
    }
    LastCheckTick = Now;

    /* 失根：先 Poll（可能本拍才 Notify），再画窗。勿 LogFs 抢在窗前（落盘可堵）。 */
    if (HalUsbMscPollUnplug() == 1) {
        sRemountWant = 1;
    }
    if (FileSystemBootMediumGone()) {
        DesktopBootMediumGoneShutdown();
    }

    /*
     * 空拖优先：指针刚动过则让路热插 Scan/HubGetPortStatus、CMOS/SMM、
     * 网托脏）。否则约 5s 一拍会让 Present cli 饿死 HID → 先顿再只闪不动。
     */
    if (!GuiPointerIdleForMilliseconds(400)) {
        return;
    }

    /* story-s3：热插；拔已在 PollUnplug。旁路拔 Remount 可挂 sRemountWant */
    Hot = HalUsbMscHotPoll();
    if (Hot == 1 || sRemountWant) {
        sRemountWant = 0;
        /*
         * Notify 已在 Release 前钉 Gone。失根则勿 Remount（会 PickDefault 扶 NUC）。
         * RAM 里画大窗 → 关机。
         */
        if (FileSystemBootMediumGone()) {
            LogFs("FS: msc/4 halt (boot medium removed)\n");
            DesktopBootMediumGoneShutdown();
        }
        (void)FileSystemRemountVolumes();
        if (FileSystemBootMediumGone()) {
            LogFs("FS: msc/4 halt (boot medium removed)\n");
            DesktopBootMediumGoneShutdown();
        }
        FilesUiRefresh();
        LogFs("FS: msc/4 hotpoll remount+Files\n");
    }

    /*
     * CMOS/网托勿跟 2Hz：UIP~1Hz + 真机 CMOS 可能进 SMM，空拖规律 0.5s 一顿。
     * 钟栏 HH:MM / 网标 ~5s 看一次足够（名义 Tps*5；实墙钟更疏，见函首注释）。
     */
    if (LastRtcNetTick != 0 && (Now - LastRtcNetTick) < (UINT64)Tps * 5u) {
        return;
    }
    LastRtcNetTick = Now;

    NeedPaint = 0;
    Ok = (HalRtcGetTime(0, 0, 0, &Hour, &Minute, 0) == 0) ? 1 : 0;
    if (Ok) {
        if (!(gClockValid && Hour == gClockHour && Minute == gClockMinute)) {
            NeedPaint = 1;
        }
    }
    /* 瞬时读失败：保留上次 HH:MM；网托变仍要刷栏 */
    if (DesktopNetTrayLabelChanged() || DesktopBluetoothTrayLabelChanged()) {
        NeedPaint = 1;
    }
    if (NeedPaint) {
        sClockBarNeedPaint = 1;
    }
}

void DesktopTickClock(void) {
    int NeedPaint;

    /*
     * 图标/壁纸补刷优先于「鼠未闲就 return」：否则 BMP 已加载仍一直纯色占位。
     * 拖窗/栏钟仍要闲下来再画（全栏 Present 饿 xHCI）。
     * 钟/网托采样只在 WorkerTick（勿 Gui 1Hz SoftRtc：会密 Present/读托盘）。
     */
    /* gui-wall-defer：Worker 建完壁纸后整桌刷新（勿在 Gui 路径 Build） */
    if (DesktopWallConsumeNeedRefresh()) {
        UINT32 Sw = 0;
        UINT32 Sh = 0;

        HalVideoGetSize(&Sw, &Sh);
        if (Sw != 0 && Sh != 0) {
            GuiMarkDamage(0, 0, Sw, Sh);
        }
        RequestRefresh();
        return;
    }

    /*
     * Worker/首帧 Ensure 已加载图标则只刷图标带。
     * 勿 RequestRefresh→ThemeScene：整屏会 DrawTaskbarRaw（装卸 Chat 栏闪），
     * 且大脏区回退全屏时标签未预热会空一帧。对表：只铺壁纸块+图标+字。
     */
    if (DesktopIconsConsumeNeedRefresh()) {
        int i;
        int Any = 0;

        /*
         * 装卸后补刷：须避窗（FillRectFree + Occluded）。
         * 旧路径整块 DesktopFillRect+Raw：商店窗压在图标带上时，壁纸砸穿客户区
         * （左边缺一块 + 桌面烙印），且 Chat 画在窗里随后被 Backup 盖掉 → 装完像没装。
         */
        ComposeBeginEraseCursor();
        HalVideoClearClip();
        for (i = 0; i < DESKTOP_ICON_COUNT; i++) {
            UINT32 Ix;
            UINT32 Iy;
            UINT32 Iw;
            UINT32 Ih;

            if (!gIcons[i].Present) {
                continue;
            }
            IconBoundsPadded(&gIcons[i], 4u, &Ix, &Iy, &Iw, &Ih);
            DesktopFillRectFree(Ix, Iy, Iw, Ih);
            DrawOneIconOccluded(&gIcons[i], i == gDeskSelected);
            Any = 1;
        }
        if (Any) {
            if (!CursorHwActive()) {
                GraphicsIrqEnter();
                CursorPaint();
                GraphicsIrqLeave();
            }
            HalVideoPresentFlush();
            if (!CursorHwActive()) {
                HalVideoPresentCursor();
            }
        }
        ComposeEnd();
        return;
    }

    /*
     * 拖窗/改大小/空拖刚动：不重画任务栏。全栏 Present 带 cli 条带会饿死 xHCI。
     */
    if (GuiDragActive() || DesktopIconDragActive() || !GuiPointerIdleForMilliseconds(400)) {
        return;
    }

    /*
     * cjk-3：指针已闲才合成。Want 抽干后才 BumpPaint；此处 Refresh 补 defer。
     * 队列未空勿全桌 Refresh（半透栏叠绘 → 哆嗦）；勿再抬 sClockBarNeedPaint。
     * 有窗：只脏窗矩形（ComposeDamage），避免 ThemeScene 反复叠半透栏。
     */
    if (FontTtfTakePaint()) {
        FontTtfAckPaint();
        if (FontTtfWantCount() == 0u) {
            /*
             * 有窗：只脏窗。无窗若仍 RequestRefresh → Mark 空 → ThemeScene 整屏，
             * 半透栏再 Blend 一趟＝任务栏闪一下（cjk-3 注释同片）。
             */
            if (GuiMarkDamageActiveWindows() > 0) {
                RequestRefresh();
            }
            /* 无窗：勿借整栏重画消化 BumpPaint（半透 Present=闪） */
        }
        return;
    }

    NeedPaint = sClockBarNeedPaint;
    if (NeedPaint) {
        sClockBarNeedPaint = 0;
    }
    if (!NeedPaint) {
        return;
    }
    /*
     * 勿 BeginFront：scale≠100 时逻辑坐标直写物理 GOP → 假任务栏。
     * 须 ClearClip：Shell 客户区 clip 会让半透底被裁、开始图标仍在。
     *
     * bug-nuc-taskbar-flicker：禁止 GuiCursorHide/Show——NUC 硬件光标路径
     * Hide=CursorHwDisable，分针一跳整只光标消失再出现，看起来像栏「哆嗦」。
     * 与 Compose 同形：EraseCursor（软才 Restore）→ 铺栏 → 软 Paint → Flush。
     */
    {
        UINT32 BarY;
        UINT32 Sw;
        UINT32 Sh;

        TaskbarGeom(&BarY, &Sw, &Sh);
        HalVideoClearClip();
        ComposeBeginEraseCursor();
        DesktopFillRect(0, BarY, Sw, TASKBAR_H);
        DrawTaskbarRaw();
        if (gMenuOpen) {
            DrawStartMenuRaw();
        }
        if (DesktopContextMenuIsOpen()) {
            DesktopDrawContextMenu();
        }
        if (DesktopNetTrayIsOpen()) {
            DesktopNetTrayDrawPopup();
        }
        if (DesktopBluetoothTrayIsOpen()) {
            DesktopBluetoothTrayDrawPopup();
        }
        if (!CursorHwActive()) {
            GraphicsIrqEnter();
            CursorPaint();
            GraphicsIrqLeave();
        }
        HalVideoPresentFlush();
        /* Pi 软光标：栏 Present 后须再刷光标脏，否则闲置一跳钟箭头消失 */
        if (!CursorHwActive()) {
            HalVideoPresentCursor();
        }
        ComposeEnd();
    }
}
