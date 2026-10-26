/*
 * WindowManager.c — 窗口全局态、开机和标题刷新（PR-S-guiwm-split-2）
 *
 * 模块表 GuiInitialize（软成功：Db/Theme 失败仍 return 0，见函数末）：
 *   PollInput → Logo(96) → Db/字体 → Theme/Locale → UI scale
 *   → LogoFinish → GuiDesktopInitialize → DrainMouse
 * GuiDesktopInitialize：屏/光标 → DesktopInitialize → 登记 Ops → Redraw
 *   → GOP mute → 鼠交接 → BootLogFlush
 * 开窗见 GuiOpen.c；点击与鼠标见 GuiPointer.c；命中见 GuiHit.c。
 */
#include "GuiPrivate.h"
#include "UI.h"
#include "HalVideo.h"
#include "HalSerial.h"
#include "Hal.h"
#include "Debug.h"
#include "Theme.h"
#include "Desktop.h"
#include "SettingsUi.h"
#include "FilesUi.h"
#include "StoreUi.h"
#include "DevicesUi.h"
#include "EditUi.h"
#include "TtyUi.h"
#include "Locale.h"
#include "CoreOps.h"
#include "HalDevices.h"
#include "KernelModulesPrivate.h"
#include "Font.h"
#include "Db.h"
#include "LogFile.h"
#include "SerialLog.h"
#include "Console.h"

GUI_WINDOW_MANAGER gGuiWindowManager = {
    .HoverWin = -1,
    .DragWin = -1,
    .ResizeWin = -1,
    .InputOwner = -1,
    .InputButton = -1,
};

void GuiRegisterConsoleOps(const GUI_CONSOLE_OPS *Ops) {
    if (!Ops) {
        gGuiWindowManager.ConsoleOps.FocusSave = 0;
        gGuiWindowManager.ConsoleOps.FocusLoad = 0;
        gGuiWindowManager.ConsoleOps.OnShellOpened = 0;
        gGuiWindowManager.ConsoleOps.PaintShellWindow = 0;
        return;
    }
    gGuiWindowManager.ConsoleOps = *Ops;
}

GUI_WIN_KIND GuiWindowKind(int Idx) {
    if (Idx < 0 || Idx >= MAX_WINS || !gGuiWindowManager.Windows[Idx].Active) {
        return GUI_WIN_NONE;
    }
    return gGuiWindowManager.Windows[Idx].Kind;
}


GUI_WIN_KIND GuiFocusKind(void) {
    return GuiWindowKind(gGuiWindowManager.FocusWin);
}


void GuiRefreshTitles(void) {
    int i;

    for (i = 0; i < MAX_WINS; i++) {
        if (!gGuiWindowManager.Windows[i].Active) {
            continue;
        }
        if (gGuiWindowManager.Windows[i].Kind == GUI_WIN_SHELL) {
            gGuiWindowManager.Windows[i].Title = LocStr(MSG_APP_SHELL);
        } else if (gGuiWindowManager.Windows[i].Kind == GUI_WIN_SETTINGS) {
            gGuiWindowManager.Windows[i].Title = LocStr(MSG_APP_SETTINGS);
        } else if (gGuiWindowManager.Windows[i].Kind == GUI_WIN_STORE) {
            gGuiWindowManager.Windows[i].Title = LocStr(MSG_APP_STORE);
        } else if (gGuiWindowManager.Windows[i].Kind == GUI_WIN_DEVICES) {
            gGuiWindowManager.Windows[i].Title = LocStr(MSG_APP_DEVICES);
        } else if (gGuiWindowManager.Windows[i].Kind == GUI_WIN_FILES) {
            gGuiWindowManager.Windows[i].Title = LocStr(MSG_APP_FILES);
        } else if (gGuiWindowManager.Windows[i].Kind == GUI_WIN_EDIT) {
            gGuiWindowManager.Windows[i].Title = LocStr(MSG_APP_EDIT);
        } else if (gGuiWindowManager.Windows[i].Kind == GUI_WIN_TTY) {
            gGuiWindowManager.Windows[i].Title = LocStr(MSG_APP_TTY);
        }
    }
    SyncWindowVisualsEx(1);
    if (GuiFocusKind() == GUI_WIN_SETTINGS) {
        SettingsUiRepaint();
    } else if (GuiFocusKind() == GUI_WIN_STORE) {
        StoreUiRepaint();
    } else if (GuiFocusKind() == GUI_WIN_DEVICES) {
        DevicesUiRepaint();
    } else if (GuiFocusKind() == GUI_WIN_FILES) {
        FilesUiRepaint();
    } else if (GuiFocusKind() == GUI_WIN_EDIT) {
        EditUiRepaint();
    } else if (GuiFocusKind() == GUI_WIN_TTY) {
        TtyUiRepaint();
    }
}


void GuiDesktopInitialize(void) {
    HalVideoGetSize(&gGuiWindowManager.ScreenWidth, &gGuiWindowManager.ScreenHeight);
    if (gGuiWindowManager.ScreenWidth == 0) {
        gGuiWindowManager.ScreenWidth = 1024;
        gGuiWindowManager.ScreenHeight = 768;
    }
    gGuiWindowManager.CursorX = gGuiWindowManager.ScreenWidth / 2;
    gGuiWindowManager.CursorY = gGuiWindowManager.ScreenHeight / 2;
    /*
     * 相对 USB 鼠把像素累加在驱动 Abs；PHOTO/ThemeLoad 期 HalInputPoll
     * 会把 Abs 推到右下。只画居中十字不同步 → 首动 dequeue 仍是旧 Abs → 飞角。
     */
    HalInputMouseHandoffDesktop(gGuiWindowManager.CursorX, gGuiWindowManager.CursorY);
    gGuiWindowManager.FocusWin = -1;
    /* 光标延到 Worker msc/3 Secondary 后再 Reveal（CursorUsbReady） */
    gGuiWindowManager.CursorVisible = 0;
    gGuiWindowManager.CursorKind = CURSOR_KIND_ARROW;
    gGuiWindowManager.DragWin = -1;
    gGuiWindowManager.ResizeWin = -1;
    gGuiWindowManager.ResizeEdge = RESIZE_EDGE_NONE;

    {
        int i;

        for (i = 0; i < MAX_WINS; i++) {
            gGuiWindowManager.Windows[i].Active = 0;
            gGuiWindowManager.Windows[i].Kind = GUI_WIN_NONE;
            gGuiWindowManager.Windows[i].TermSet = 0;
            gGuiWindowManager.Windows[i].InputLen = 0;
            gGuiWindowManager.Windows[i].WaitPrompt = 0;
            gGuiWindowManager.Windows[i].PromptShown = 0;
            gGuiWindowManager.Windows[i].InputLine[0] = 0;
            gGuiWindowManager.Windows[i].Title = "";
            gGuiWindowManager.Windows[i].Background = ThemeShellClientBackground();
        }
    }

    PreallocWindowBackups();
    DesktopSetPointOccupied(GuiPointInAnyWindow);
    DesktopSetRequestRefresh(GuiRefreshDesktop);
    DesktopSetClearIconFootprint(GuiClearIconDragFootprint);
    DesktopInitialize();
    /*
     * 首帧前同步灌 BMP：ModulesRun 时尚无 Worker 并发，读盘安全。
     * 仅靠 Worker+TickClock 时，鼠未闲 400ms 会一直停在纯色占位。
     */
    DesktopEnsureIconsLoaded();
    {
        WINDOW_OPS Ops;

        Ops.OpenUser = GuiOpenUser;
        Ops.DamageUser = GuiDamageUser;
        Ops.DamageRectUser = GuiDamageRectUser;
        Ops.PollUserInput = GuiPollUserInput;
        Ops.AddButton = GuiUserAddButton;
        WindowOpsRegister(&Ops);
    }
    {
        DESKTOP_OPS Desk;

        Desk.PollMouseMotion = GuiPollMouseMotion;
        Desk.CloseAllUserWindows = GuiCloseAllUserWindows;
        Desk.WaitNoWindowClosing = GuiWaitNoWindowClosing;
        Desk.ComposeThemeScene = GuiComposeThemeScene;
        DesktopOpsRegister(&Desk);
    }
    /*
     * 首帧桌面：先停 GOP、整屏黑 sync，再一次 PresentFlush（抬高条带）。
     * 避免 U-Boot/boot 字与桌面条带撕开「底半先出任务栏」。
     * SCREEN_LOG：盖屏前 HOLD，方便读 DWC2/串口诊断行。
     */
#if SCREEN_LOG
    HalSerialBootHoldMs(8000u);
#endif
    HalSerialGopMirror(0);
    HalSerialGopMute(1);
    {
        UINT32 W = gGuiWindowManager.ScreenWidth;
        UINT32 H = gGuiWindowManager.ScreenHeight;

        HalVideoDrawBeginFront();
        HalVideoFillRect(0, 0, W, H, 0x00000000u);
        HalVideoDrawEndFront();
    }
    HalVideoSetPresentChunkRows(16384u);
    GuiRedraw();
    HalVideoSetPresentChunkRows(0);
    /* #117：仅漏绑时补鼠（InitMouseDeferred 内部已绑则跳过）；坐标对齐一次即可 */
    HalInputInitializeMouseDeferred();
    HalInputMouseHandoffDesktop(gGuiWindowManager.CursorX, gGuiWindowManager.CursorY);
    /*
     * Pi DWC2：鼠在 Mod Gui 前已 Attach；真机 CNTV 无 IRQ → CondResched
     * 切不到 Worker Reveal。桌面已 Present 且鼠在则此处露光标（幂等）。
     */
    if (HalMousePresent()) {
        GuiCursorRevealAfterUsb();
        HalSerialBootMark("Boot: cursor reveal (mouse already attached)\n");
    }
    HalSerialBootMark("Boot: save BOOT.LOG\n");
    BootLogFlush();
    LogGui("Gui: Desktop Ready\n");
    /*
     * 勿在此 Announce：后面还有 Network 模块。
     * 真机：KernelAfterModules（Modules 全完）Announce；virt：Worker 末。
     */
}

static void PollInput(void) {
    if (!HalCpuIsHypervisor()) {
        HalInputPoll();
    }
}

static void DrainMouse(void) {
    HAL_MOUSE_REPORT Mouse;

    if (HalCpuIsHypervisor()) {
        return;
    }
    HalInputPoll();
    while (HalMouseDequeue(&Mouse)) {
    }
}

int GuiInitialize(void) {
    PollInput();
    /* Logo 已在 Video 后缓冲就绪时画出；这里继续读字体主题，仍盖在 Logo 下 */
    DesktopBootLogoSet(95);
    (void)DbInitialize();
    (void)FontLoadAssets();
    (void)FontTtfLoad();
    (void)FontTtfInit();
    (void)FontTtfMonoInit();
    /*
     * FT 须在 Locale 预热前：否则开机 stb HIT 占槽，Worker 再 Init 也
     * DropCjkHits 风暴 → 鼠死；不 Drop 则汉字永远旧 stb。
     */
    (void)FontFtInit();
    /* cjk-4：UMING/DROID 进字体表，供设置→字体 / Theme font= 名 */
    FontNotifyTtfReady();
    PollInput();
    (void)ThemeLoad();
    LocaleInitialize();
    PollInput();
    if (ThemeUiScale() != 100) {
        (void)HalVideoSetUiScale(ThemeUiScale());
    }
    DesktopBootLogoSet(100);
    DesktopBootLogoFinish();
    GuiDesktopInitialize();
    DrainMouse();
    /* 软成功：库/主题/字缺失不挡进桌面（模块表仍前进） */
    return 0;
}
