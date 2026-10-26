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
 * 旁路 U 盘热拔：不关机。U 盘起拔启动盘：Gone → 提示后关机（usb-boot-unplug）。
 * 钟/网托→栏脏；Present 仍 DesktopTickClock（指针闲才画栏）。勿 Worker 上全量 Compose。
 */
#include "DesktopPrivate.h"
#include "Gui.h" /* GuiCursorHide/Show；GuiDragActive */
#include "FilesUi.h"
#include "FileSystem.h"
#include "Locale.h"
#include "SerialLog.h"
#include "Hal.h"
#include "HalVideo.h"

#define BOOT_GONE_SECS 10u

static volatile int sClockBarNeedPaint;

static void BootGonePaint(const char *Title, const char *Body,
                          const char *CountLine) {
    UINT32 Sw = 0;
    UINT32 Sh = 0;
    UINT32 BoxW;
    UINT32 BoxH;
    UINT32 BoxX;
    UINT32 BoxY;
    UINT32 CellH;
    UINT32 Tw;
    UINT32 Pad = 32u;
    UINT32 Y;

    HalVideoGetSize(&Sw, &Sh);
    if (Sw < 320u || Sh < 200u) {
        Sw = 1024;
        Sh = 768;
    }
    CellH = FontCellH();
    if (CellH < 16u) {
        CellH = 16u;
    }
    BoxW = Sw * 3u / 5u;
    if (BoxW < 400u) {
        BoxW = Sw > 40u ? Sw - 40u : Sw;
    }
    if (BoxW > Sw - 40u) {
        BoxW = Sw > 40u ? Sw - 40u : Sw;
    }
    BoxH = Pad * 2u + CellH * 5u + 24u;
    BoxX = (Sw - BoxW) / 2u;
    BoxY = (Sh - BoxH) / 2u;

    HalVideoFillRect(0, 0, Sw, Sh, 0x00101018u);
    HalVideoFillRect(BoxX, BoxY, BoxW, BoxH, ThemeDialogFace());
    HalVideoFillRect(BoxX, BoxY, BoxW, 3u, ThemeDialogBorder());
    HalVideoFillRect(BoxX, BoxY + BoxH - 3u, BoxW, 3u, ThemeDialogBorder());
    HalVideoFillRect(BoxX, BoxY, 3u, BoxH, ThemeDialogBorder());
    HalVideoFillRect(BoxX + BoxW - 3u, BoxY, 3u, BoxH, ThemeDialogBorder());

    Y = BoxY + Pad;
    Tw = FontStringWidth(Title);
    HalVideoDrawStringAt(BoxX + (BoxW - Tw) / 2u, Y, Title, ThemeText());
    Y += CellH + 16u;
    Tw = FontStringWidth(Body);
    HalVideoDrawStringAt(BoxX + (BoxW - Tw) / 2u, Y, Body, ThemeTextMuted());
    Y += CellH + 20u;
    Tw = FontStringWidth(CountLine);
    HalVideoDrawStringAt(BoxX + (BoxW - Tw) / 2u, Y, CountLine, ThemeText());
    HalVideoPresent();
}

static void BootGoneBuildCount(char *Out, UINTN OutMax, UINT32 Sec) {
    const char *Before;
    const char *After;
    UINTN i = 0;
    UINTN j;

    if (!Out || OutMax < 8u) {
        return;
    }
    Before = LocStr(MSG_BOOT_GONE_BEFORE);
    After = LocStr(MSG_BOOT_GONE_AFTER);
    if (!Before) {
        Before = "";
    }
    if (!After) {
        After = "";
    }
    for (j = 0; Before[j] && i + 1u < OutMax; j++) {
        Out[i++] = Before[j];
    }
    if (i + 1u < OutMax) {
        Out[i++] = ' ';
    }
    if (Sec >= 10u && i + 1u < OutMax) {
        Out[i++] = (char)('0' + (Sec / 10u) % 10u);
    }
    if (i + 1u < OutMax) {
        Out[i++] = (char)('0' + Sec % 10u);
    }
    if (i + 1u < OutMax) {
        Out[i++] = ' ';
    }
    for (j = 0; After[j] && i + 1u < OutMax; j++) {
        Out[i++] = After[j];
    }
    Out[i] = 0;
}

/* 等 CMOS 秒跳变一次（真机 HalTicksPerSec=250 常偏慢→倒数翻倍快，勿用 LAPIC 计秒） */
static void BootGoneWaitRtcSecond(void) {
    UINT8 Sec0 = 0;
    UINT8 Sec1 = 0;
    HAL_MOUSE_REPORT Mouse;
    HAL_KEYBOARD_REPORT Key;
    UINT32 Guard = 0;
    UINT32 Tps;
    UINT64 Deadline;

    if (HalRtcGetTime(0, 0, 0, 0, 0, &Sec0) != 0) {
        /* RTC 不可用：按标称 Tps 等 1s（可能仍偏，但有兜底） */
        Tps = HalTicksPerSec();
        if (Tps == 0) {
            Tps = 250;
        }
        Deadline = HalCpuTicks(0) + (UINT64)Tps;
        while (HalCpuTicks(0) < Deadline) {
            while (HalMouseDequeue(&Mouse)) {
            }
            while (HalKeyboardDequeue(&Key)) {
            }
            HalCpuRelax();
        }
        return;
    }
    for (;;) {
        while (HalMouseDequeue(&Mouse)) {
        }
        while (HalKeyboardDequeue(&Key)) {
        }
        if (HalRtcGetTime(0, 0, 0, 0, 0, &Sec1) == 0 && Sec1 != Sec0) {
            return;
        }
        HalCpuRelax();
        /* 防 RTC 卡住：约 3s 标称拍后放弃本秒 */
        if ((++Guard & 0xFFFFFu) == 0) {
            return;
        }
    }
}

void DesktopBootMediumGoneShutdown(void) {
    const char *Title;
    const char *Body;
    char CountLine[96];
    UINT32 Left;
    HAL_MOUSE_REPORT Mouse;
    HAL_KEYBOARD_REPORT Key;

    Title = LocStr(MSG_BOOT_GONE_TITLE);
    Body = LocStr(MSG_BOOT_GONE_BODY);
    if (!Title || Title[0] == 0) {
        Title = "Boot disk removed";
    }
    if (!Body || Body[0] == 0) {
        Body = "The system cannot continue.";
    }

    /*
     * 锁输入；按 RTC 秒跳倒数（勿用 HalTicksPerSec——NUC 上 250 拍/s 与墙钟不符，
     * 曾见 1 墙钟秒跳 2 个数）。
     */
    GuiInputLock(1);
    GuiCursorHide();
    HalVideoClearClip();

    for (Left = BOOT_GONE_SECS; Left >= 1u; Left--) {
        BootGoneBuildCount(CountLine, sizeof(CountLine), Left);
        BootGonePaint(Title, Body, CountLine);
        while (HalMouseDequeue(&Mouse)) {
        }
        while (HalKeyboardDequeue(&Key)) {
        }
        BootGoneWaitRtcSecond();
    }

    BootGoneBuildCount(CountLine, sizeof(CountLine), 0);
    BootGonePaint(Title, Body, CountLine);
    LogFs("Fs: msc/4 shutdown (boot medium removed)\n");
    HalCpuShutdown();
    for (;;) {
        HalCpuPark();
    }
}

void DesktopNotifyNetChanged(void) {
    /*
     * 与 Worker 网托采样同终点：栏脏 + Mark，Gui TickClock 闲时 DrawTaskbar。
     * DHCP Adopt / setip 后立刻可绿；勿另起托盘 API。
     */
    sClockBarNeedPaint = 1;
    DesktopMarkTaskbar();
    if (DesktopNetTrayIsOpen()) {
        DesktopMarkNetTrayPopup();
    }
    RequestRefresh();
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

    Now = HalCpuTicks(0);
    Tps = HalTicksPerSec();
    if (Tps == 0) {
        Tps = 250;
    }
    /* HotPoll 入口仍 ~2Hz（内部拔检/热插已 5s 节流） */
    if (LastCheckTick != 0 && (Now - LastCheckTick) < (UINT64)(Tps / 2u)) {
        return;
    }
    LastCheckTick = Now;

    /* 失根：指针忙也检（勿整段 return 漏大窗） */
    if (HalUsbMscPollUnplug() == 1) {
        if (FileSystemBootMediumGone()) {
            LogFs("Fs: msc/4 halt (boot medium removed)\n");
            DesktopBootMediumGoneShutdown();
        }
        /* Gone 未钉时才 Remount（旁路拔）；已 Gone 则上面不返回 */
        sRemountWant = 1;
    }

    /*
     * 空拖优先：指针刚动过则让路热插 Scan/HubGetPortStatus、CMOS/SMM、
     * 网托脏）。否则约 5s 一拍会让 Present cli 饿死 HID → 先顿再只闪不动。
     */
    if (!GuiPointerIdleForMs(400)) {
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
            LogFs("Fs: msc/4 halt (boot medium removed)\n");
            DesktopBootMediumGoneShutdown();
        }
        (void)FileSystemRemountVolumes();
        if (FileSystemBootMediumGone()) {
            LogFs("Fs: msc/4 halt (boot medium removed)\n");
            DesktopBootMediumGoneShutdown();
        }
        FilesUiRefresh();
        LogFs("Fs: msc/4 hotpoll remount+Files\n");
    }

    /*
     * CMOS/网托勿跟 2Hz：UIP~1Hz + 真机 CMOS 可能进 SMM，空拖规律 0.5s 一顿。
     * 钟栏 HH:MM / 网标 ~5s 看一次足够。
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
    if (DesktopNetTrayLabelChanged()) {
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

    /* Worker/首帧 Ensure 已加载图标则只刷新；勿在 Gui 路径读盘 */
    if (DesktopIconsConsumeNeedRefresh()) {
        int i;
        int Any = 0;
        UINT32 X0 = 0;
        UINT32 Y0 = 0;
        UINT32 X1 = 0;
        UINT32 Y1 = 0;

        for (i = 0; i < DESKTOP_ICON_COUNT; i++) {
            UINT32 R;
            UINT32 B;

            if (!gIcons[i].Present) {
                continue;
            }
            R = gIcons[i].X + DESKTOP_ICON_SIZE;
            B = gIcons[i].Y + DESKTOP_ICON_SIZE + DESKTOP_LABEL_PAD + 64u;
            if (!Any) {
                X0 = gIcons[i].X;
                Y0 = gIcons[i].Y;
                X1 = R;
                Y1 = B;
                Any = 1;
            } else {
                if (gIcons[i].X < X0) {
                    X0 = gIcons[i].X;
                }
                if (gIcons[i].Y < Y0) {
                    Y0 = gIcons[i].Y;
                }
                if (R > X1) {
                    X1 = R;
                }
                if (B > Y1) {
                    Y1 = B;
                }
            }
        }
        if (Any && X1 > X0 && Y1 > Y0) {
            GuiMarkDamage(X0, Y0, X1 - X0, Y1 - Y0);
        }
        RequestRefresh();
        return;
    }

    /*
     * 拖窗/改大小/空拖刚动：不重画任务栏。全栏 Present 带 cli 条带会饿死 xHCI。
     */
    if (GuiDragActive() || DesktopIconDragActive() || !GuiPointerIdleForMs(400)) {
        return;
    }

    /*
     * cjk-3：指针已闲才合成。WantDrain/Preheat 灌 HIT 后 BumpPaint；
     * 此处 RequestRefresh 补 defer 空白。忙时上面已 return，勿与鼠抢。
     */
    if (FontTtfTakePaint()) {
        FontTtfAckPaint();
        sClockBarNeedPaint = 1;
        DesktopMarkTaskbar();
        RequestRefresh();
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
     * 后缓冲 + Present；先擦光标再铺栏，避 Alpha/开始钮烙印。
     * 须 ClearClip：Shell 客户区 clip 会让半透底被裁、开始图标仍在。
     */
    {
        UINT32 BarY;
        UINT32 Sw;
        UINT32 Sh;

        TaskbarGeom(&BarY, &Sw, &Sh);
        HalVideoClearClip();
        GuiCursorHide();
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
        GuiCursorShow();
        HalVideoPresent();
    }
}
