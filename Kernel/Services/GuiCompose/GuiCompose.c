/*
 * GuiCompose.c — Present / 主题场景编排（PR-S-compose-split-1）
 *
 * 窗绘制见 GuiDraw.c；备份见 GuiBackup.c。
 */
#include "GuiPrivate.h"
#include "UI.h"
#include "HalVideo.h"
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
#include "Scheduler.h"

void GfxIrqEnter(void) {
    if (gGuiWindowManager.GfxLockDepth++ == 0) {
        SchedulerPreemptDisable();
        gGuiWindowManager.GfxIrqFlags = HalIrqSave();
    }
}


void GfxIrqLeave(void) {
    if (gGuiWindowManager.GfxLockDepth > 0 && --gGuiWindowManager.GfxLockDepth == 0) {
        HalIrqRestore(gGuiWindowManager.GfxIrqFlags);
        SchedulerPreemptEnable();
    }
}


void ComposeBegin(void) {
    if (gGuiWindowManager.ComposeBusy++ == 0) {
        SchedulerPreemptDisable();
    }
}


void ComposeEnd(void) {
    if (gGuiWindowManager.ComposeBusy > 0 && --gGuiWindowManager.ComposeBusy == 0) {
        SchedulerPreemptEnable();
    }
}

/* 先擦光标再置 Busy，避免 SMP 上 Busy 抢在 Restore 前清 Visible → 镂空 */
void ComposeBeginEraseCursor(void) {
    GfxIrqEnter();
    CursorRestore();
    ComposeBegin();
    GfxIrqLeave();
}


/* 主题合成中推迟 Present；拖动等路径仍立即提交 */
void GfxPresent(void) {
    if (gGuiWindowManager.DeferPresent) {
        gGuiWindowManager.ShellEchoCoalesce = 0;
        return;
    }
    /*
     * PR-G-shell-present：Shell 打字回显先写后缓冲，跳过本帧 Present。
     * ShellTask 每轮末尾 HalVideoPresent 会合并刷出；help/ls 仍走 Defer*。
     */
    if (gGuiWindowManager.ShellEchoCoalesce) {
        gGuiWindowManager.ShellEchoCoalesce = 0;
        return;
    }
    HalVideoPresent();
}

void GuiPresentShellEchoMark(void) {
    gGuiWindowManager.ShellEchoCoalesce = 1;
}

void GuiPresentDeferPush(void) {
    gGuiWindowManager.DeferPresent++;
}

void GuiPresentDeferPop(void) {
    if (gGuiWindowManager.DeferPresent > 0) {
        gGuiWindowManager.DeferPresent--;
    }
    gGuiWindowManager.ShellEchoCoalesce = 0;
    if (gGuiWindowManager.DeferPresent == 0) {
        HalVideoPresent();
    }
}

/* 可嵌套：仅最外层把计数清零；Resume 还原。供 Shell 等 StoreJob 时放行光标 Present */
static int gDeferPauseDepth;
static int gDeferSaved;

void GuiPresentDeferPause(void) {
    if (gDeferPauseDepth++ == 0) {
        gDeferSaved = gGuiWindowManager.DeferPresent;
        gGuiWindowManager.DeferPresent = 0;
        gGuiWindowManager.ShellEchoCoalesce = 0;
    }
}

void GuiPresentDeferResume(void) {
    if (gDeferPauseDepth > 0 && --gDeferPauseDepth == 0) {
        gGuiWindowManager.DeferPresent = gDeferSaved;
        gDeferSaved = 0;
    }
}


void GuiRedraw(void) {
    int i;

    /* G7：桌面/窗体开中断绘制；ComposeBusy 丢弃嵌套鼠标；只锁光标 */
    ComposeBeginEraseCursor();
    HalVideoClearClip();
    DesktopFillRect(0, 0, gGuiWindowManager.ScreenWidth, gGuiWindowManager.ScreenHeight);
    DesktopDraw();
    for (i = 0; i < MAX_WINS; i++) {
        DrawWindowAt(i);
    }
    DesktopDrawStartMenu();
    DesktopDrawNetTrayPopup();
    GfxIrqEnter();
    CursorPaint();
    GfxIrqLeave();
    HalVideoPresentFlush();
    ComposeEnd();
}


void GuiApplyThemeColors(void) {
    int i;
    UINT32 Bg = ThemeShellClientBackground();

    /* 只更新属性；整屏提交见 GuiComposeThemeScene（PR-G8） */
    for (i = 0; i < MAX_WINS; i++) {
        if (gGuiWindowManager.Windows[i].Active && gGuiWindowManager.Windows[i].Kind == GUI_WIN_SHELL) {
            gGuiWindowManager.Windows[i].Background = Bg;
            gGuiWindowManager.Windows[i].TermSet = 0;
            gGuiWindowManager.Windows[i].TermX = 0;
            gGuiWindowManager.Windows[i].TermY = 0;
            gGuiWindowManager.Windows[i].InputLen = 0;
            gGuiWindowManager.Windows[i].InputLine[0] = 0;
            /* 勿清 PromptShown：否则 Compose 后切回 Shell 会在 toyos> 后再打欢迎语 */
            gGuiWindowManager.Windows[i].WaitPrompt = 0;
        } else if (gGuiWindowManager.Windows[i].Active && gGuiWindowManager.Windows[i].Kind == GUI_WIN_SETTINGS) {
            gGuiWindowManager.Windows[i].Background = ThemeSettingsClientBackground();
        } else if (gGuiWindowManager.Windows[i].Active && gGuiWindowManager.Windows[i].Kind == GUI_WIN_STORE) {
            gGuiWindowManager.Windows[i].Background = ThemeSettingsClientBackground();
        } else if (gGuiWindowManager.Windows[i].Active && gGuiWindowManager.Windows[i].Kind == GUI_WIN_DEVICES) {
            gGuiWindowManager.Windows[i].Background = ThemeSettingsClientBackground();
        } else if (gGuiWindowManager.Windows[i].Active && gGuiWindowManager.Windows[i].Kind == GUI_WIN_FILES) {
            gGuiWindowManager.Windows[i].Background = ThemeSettingsClientBackground();
        } else if (gGuiWindowManager.Windows[i].Active && gGuiWindowManager.Windows[i].Kind == GUI_WIN_EDIT) {
            gGuiWindowManager.Windows[i].Background = ThemeSettingsClientBackground();
        } else if (gGuiWindowManager.Windows[i].Active && gGuiWindowManager.Windows[i].Kind == GUI_WIN_TTY) {
            gGuiWindowManager.Windows[i].Background = ThemeShellClientBackground();
        }
    }
}


/*
 * PR-G8/G9：主题一次合成（painter's algorithm，后缓冲上完成再 Present）：
 * 1) 整屏桌面 + 图标；2) 自下而上不透明整窗 + 内容；每窗立刻 ForceFull 备份；
 * gGuiWindowManager.DeferPresent 避免中间态刷到 GOP（灰闪 / 下层盖上层）。
 */
void GuiComposeThemeScene(void) {
    int i;
    int SavedFocus = gGuiWindowManager.FocusWin;

    gGuiWindowManager.DeferPresent = 1;
    ComposeBeginEraseCursor();
    HalVideoClearClip();

    /* 先铺底：有 DeferPresent 时整屏 wipe 不会露到屏幕 */
    DesktopFillRect(0, 0, gGuiWindowManager.ScreenWidth, gGuiWindowManager.ScreenHeight);
    DesktopDraw();

    for (i = 0; i < MAX_WINS; i++) {
        if (!gGuiWindowManager.Windows[i].Active) {
            continue;
        }
        /*
         * 标题栏颜色看 gGuiWindowManager.FocusWin。画 Shell 内容时会暂把焦点设到该窗；
         * 若不先恢复 SavedFocus，后画的 Settings 标题会被画成灰色（失焦）。
         */
        gGuiWindowManager.FocusWin = SavedFocus;
        DrawWindowAtEx(i, 0);
        if (gGuiWindowManager.Windows[i].Kind == GUI_WIN_SHELL) {
            gGuiWindowManager.FocusWin = i;
            GuiConsoleOpsPaintShellWindow(i);
        } else if (gGuiWindowManager.Windows[i].Kind == GUI_WIN_SETTINGS) {
            gGuiWindowManager.FocusWin = i;
            SettingsUiPaintFocused();
        } else if (gGuiWindowManager.Windows[i].Kind == GUI_WIN_STORE) {
            gGuiWindowManager.FocusWin = i;
            StoreUiPaintFocused();
        } else if (gGuiWindowManager.Windows[i].Kind == GUI_WIN_DEVICES) {
            gGuiWindowManager.FocusWin = i;
            DevicesUiPaintFocused();
        } else if (gGuiWindowManager.Windows[i].Kind == GUI_WIN_FILES) {
            gGuiWindowManager.FocusWin = i;
            FilesUiPaintFocused();
        } else if (gGuiWindowManager.Windows[i].Kind == GUI_WIN_EDIT) {
            gGuiWindowManager.FocusWin = i;
            EditUiPaintFocused();
        } else if (gGuiWindowManager.Windows[i].Kind == GUI_WIN_TTY) {
            gGuiWindowManager.FocusWin = i;
            TtyUiPaintFocused();
        } else if (gGuiWindowManager.Windows[i].Kind == GUI_WIN_USER) {
            gGuiWindowManager.FocusWin = i;
            PaintUserClient(i);
        }
        /* 上层尚未画上：整窗备份，避免重叠区镂空透视 */
        BackupWindowAtEx(i, 1);
    }

    /* 全部 ForceFull 备份完成后再画影，避免下层备份吸入上层阴影 */
    for (i = 0; i < MAX_WINS; i++) {
        if (gGuiWindowManager.Windows[i].Active) {
            DrawWindowShadowAt(i);
        }
    }

    /* 开始菜单 / 网络托盘盖住所有窗；须清 Shell 客户区 clip（FillRect/文字受 clip，BMP 图标走 WriteRect 不受） */
    HalVideoClearClip();
    DesktopDrawStartMenu();
    DesktopDrawNetTrayPopup();

    gGuiWindowManager.FocusWin = SavedFocus;
    if (SavedFocus >= 0 && SavedFocus < MAX_WINS && gGuiWindowManager.Windows[SavedFocus].Active) {
        GuiFocusApply();
    } else {
        HalVideoClearClip();
    }
    GfxIrqEnter();
    CursorPaint();
    GfxIrqLeave();
    gGuiWindowManager.DeferPresent = 0;
    /*
     * PresentFlush 必须仍在 ComposeBusy 内：NUC 8 核上 AP 鼠标否则会
     * CursorRestore 旧 gGuiWindowManager.Under → 正方形镂空；任务栏半截 Present 同窗。
     */
    HalVideoPresentFlush();
    ComposeEnd();
}


void GuiPaintWindow(int Idx) {
    if (Idx < 0 || Idx >= MAX_WINS || !gGuiWindowManager.Windows[Idx].Active) {
        return;
    }
    GfxIrqEnter();
    CursorRestore();
    DrawWindowAt(Idx);
    CursorPaint();
    HalVideoPresent();
    GfxIrqLeave();
}


void GuiBackupAllWindows(void) {
    int i;

    for (i = 0; i < MAX_WINS; i++) {
        if (gGuiWindowManager.Windows[i].Active) {
            BackupWindowAt(i);
        }
    }
}

