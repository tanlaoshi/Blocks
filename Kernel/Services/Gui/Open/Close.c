/*
 * Close.c — 关窗与 Closing 等待（gui-g3-close）
 *
 * CloseWindow 合成分叉（WasUser 在槽清空前取样）：
 *   USER — 轻量：擦本窗 → 相交窗重画 → 清 clip 画任务栏 → PresentFlush（Busy 内）
 *          禁止 GuiComposeThemeScene（4K 全屏会拖死 GuiWaitNoWindowClosing
 *          → Destroy 抢页 → #PF）。完整合成在 SchedulerExitUser Destroy 之后。
 *   非 USER — 全窗 BackupValid 清零 → GuiComposeThemeScene（故意全屏）
 * GuiWaitNoWindowClosing：自旋至 Closing 清零；禁止超时强拆页表。
 * 断言：USER 轻量期间 GuiCloseUserLightActive=1；ThemeScene 入口见则 halt。
 * 调用面四档见 Damage.c（gui-g2-mark）。开窗辅助 Open.c。
 */
#include "GuiPrivate.h"
#include "UI.h"
#include "HalVideo.h"
#include "Hal.h"
#include "Debug.h"
#include "Theme.h"
#include "PhysicalMemory.h"
#include "SettingsUi.h"
#include "FilesUi.h"
#include "StoreUi.h"
#include "DevicesUi.h"
#include "EditUi.h"
#include "TtyUi.h"
#include "Desktop.h"
#include "DesktopPrivate.h"
#include "Console.h"

static int gCloseUserLight;

int GuiCloseUserLightActive(void) {
    return gCloseUserLight;
}

void CloseWindow(int Idx) {
    UINT32 X;
    UINT32 Y;
    UINT32 Ww;
    UINT32 Wh;
    int i;
    int SavedFocus;
    int WasUser;

    if (Idx < 0 || Idx >= MAX_WINS || !gGuiWindowManager.Windows[Idx].Active) {
        return;
    }
    if (gGuiWindowManager.Windows[Idx].Closing) {
        return;
    }
    gGuiWindowManager.Windows[Idx].Closing = 1;
    WasUser = (gGuiWindowManager.Windows[Idx].Kind == GUI_WIN_USER);
    if (gGuiWindowManager.Windows[Idx].Kind == GUI_WIN_SHELL) {
        gConsole.SbWinForget(Idx);
    }
    if (gGuiWindowManager.Windows[Idx].Kind == GUI_WIN_TTY) {
        TtyUiClose();
    }
    X = gGuiWindowManager.Windows[Idx].X;
    Y = gGuiWindowManager.Windows[Idx].Y;
    Ww = gGuiWindowManager.Windows[Idx].Width;
    Wh = gGuiWindowManager.Windows[Idx].Height;

    /*
     * USER 关窗不做淡出（真机淡出易与 exit 竞态 → 二次 exec #PF）。
     * 其它窗仍可淡出；淡出 under 只铺本窗矩形（GuiFade）。
     */
    if (!WasUser && ThemeWindowFadeSteps() != 0) {
        if (!gGuiWindowManager.WinBackupValid[Idx]) {
            BackupWindowAt(Idx);
        }
        GuiAnimateWindowFade(Idx, 0);
    }

    if (WasUser) {
        gGuiWindowManager.Windows[Idx].ClosePending = 1;
    }
    gGuiWindowManager.Windows[Idx].Active = 0;
    gGuiWindowManager.Windows[Idx].FixedSize = 0;
    gGuiWindowManager.Windows[Idx].Kind = GUI_WIN_NONE;
    gGuiWindowManager.Windows[Idx].TermSet = 0;
    gGuiWindowManager.Windows[Idx].InputLen = 0;
    gGuiWindowManager.Windows[Idx].WaitPrompt = 0;
    gGuiWindowManager.Windows[Idx].PromptShown = 0;
    gGuiWindowManager.Windows[Idx].InputLine[0] = 0;
    gGuiWindowManager.UserSlot[Idx].UserButtonClick = -1;
    gGuiWindowManager.UserSlot[Idx].UserClientClick = 0;
    gGuiWindowManager.UserSlot[Idx].UserKeyCount = 0;
    {
        int Bi;
        for (Bi = 0; Bi < 4; Bi++) {
            gGuiWindowManager.UserSlot[Idx].UserButtonUsed[Bi] = 0;
            gGuiWindowManager.UserSlot[Idx].UserButtonLabel[Bi][0] = 0;
        }
    }
    gGuiWindowManager.WinBackupValid[Idx] = 0;
    if (gGuiWindowManager.WinBackup[Idx] != 0) {
        PhysicalMemoryFreePages(gGuiWindowManager.WinBackup[Idx], gGuiWindowManager.WinBackupPages[Idx]);
        gGuiWindowManager.WinBackup[Idx] = 0;
        gGuiWindowManager.WinBackupPages[Idx] = 0;
        gGuiWindowManager.WinBackupW[Idx] = 0;
        gGuiWindowManager.WinBackupH[Idx] = 0;
    }
    if (gGuiWindowManager.DragWin == Idx) {
        gGuiWindowManager.DragWin = -1;
    }
    if (gGuiWindowManager.ResizeWin == Idx) {
        gGuiWindowManager.ResizeWin = -1;
    }
    if (gGuiWindowManager.HoverWin == Idx) {
        gGuiWindowManager.HoverWin = -1;
    }
    if (gGuiWindowManager.FocusWin == Idx) {
        gGuiWindowManager.FocusWin = -1;
        for (i = MAX_WINS - 1; i >= 0; i--) {
            if (gGuiWindowManager.Windows[i].Active) {
                gGuiWindowManager.FocusWin = i;
                break;
            }
        }
    }

    SavedFocus = gGuiWindowManager.FocusWin;

    if (WasUser) {
        UINT32 BarY;
        UINT32 Sw;
        UINT32 Sh;

        /* USER 轻量：契约见文件头（禁 ThemeScene；旗标供 ThemeScene 断言） */
        gCloseUserLight = 1;
        ComposeBeginEraseCursor();
        HalVideoClearClip();
        DesktopFillRect(X, Y, Ww, Wh);
        DesktopDrawRect(X, Y, Ww, Wh);
        for (i = 0; i < MAX_WINS; i++) {
            if (!gGuiWindowManager.Windows[i].Active) {
                continue;
            }
            if (!RectIntersects(gGuiWindowManager.Windows[i].X, gGuiWindowManager.Windows[i].Y, gGuiWindowManager.Windows[i].Width,
                                gGuiWindowManager.Windows[i].Height, X, Y, Ww, Wh)) {
                continue;
            }
            gGuiWindowManager.WinBackupValid[i] = 0;
            DrawWindowAtEx(i, 0);
            if (gGuiWindowManager.Windows[i].Kind == GUI_WIN_SHELL) {
                GuiConsoleOpsPaintShellWindow(i);
            } else if (gGuiWindowManager.Windows[i].Kind == GUI_WIN_SETTINGS) {
                gGuiWindowManager.FocusWin = i;
                SettingsUiRepaint();
                gGuiWindowManager.FocusWin = SavedFocus;
            } else if (gGuiWindowManager.Windows[i].Kind == GUI_WIN_STORE) {
                gGuiWindowManager.FocusWin = i;
                StoreUiRepaint();
                gGuiWindowManager.FocusWin = SavedFocus;
            } else if (gGuiWindowManager.Windows[i].Kind == GUI_WIN_DEVICES) {
                gGuiWindowManager.FocusWin = i;
                DevicesUiRepaint();
                gGuiWindowManager.FocusWin = SavedFocus;
            } else if (gGuiWindowManager.Windows[i].Kind == GUI_WIN_FILES) {
                gGuiWindowManager.FocusWin = i;
                FilesUiRepaint();
                gGuiWindowManager.FocusWin = SavedFocus;
            } else if (gGuiWindowManager.Windows[i].Kind == GUI_WIN_EDIT) {
                gGuiWindowManager.FocusWin = i;
                EditUiRepaint();
                gGuiWindowManager.FocusWin = SavedFocus;
            } else if (gGuiWindowManager.Windows[i].Kind == GUI_WIN_TTY) {
                gGuiWindowManager.FocusWin = i;
                TtyUiRepaint();
                gGuiWindowManager.FocusWin = SavedFocus;
            } else if (gGuiWindowManager.Windows[i].Kind == GUI_WIN_USER) {
                PaintUserClient(i);
            }
            BackupWindowAtEx(i, 1);
        }
        TaskbarGeom(&BarY, &Sw, &Sh);
        /*
         * Shell gConsole.Write 常留下客户区 clip。DesktopFillRect 走 WriteRect
         *（无视 clip）先擦掉整条栏，而 DrawTaskbarRaw 的 Fill/Alpha 受 clip
         * → 栏没了（与 DesktopTickClock 同坑，见 Desktop.c）。
         */
        HalVideoClearClip();
        DesktopFillRect(0, BarY, Sw, TASKBAR_H);
        DrawTaskbarRaw();
        gGuiWindowManager.FocusWin = SavedFocus;
        GraphicsIrqEnter();
        CursorPaint();
        GraphicsIrqLeave();
        HalVideoPresentFlush();
        ComposeEnd();
        gCloseUserLight = 0;
    } else {
        for (i = 0; i < MAX_WINS; i++) {
            if (gGuiWindowManager.Windows[i].Active) {
                gGuiWindowManager.WinBackupValid[i] = 0;
            }
        }
        GuiComposeThemeScene();
        gGuiWindowManager.FocusWin = SavedFocus;
    }

    GuiFocusApply();
    gGuiWindowManager.Windows[Idx].Closing = 0;
    DebugWrite("Gui: closed (clip)\n");
}

void GuiCloseAllUserWindows(void) {
    int i;

    for (i = MAX_WINS - 1; i >= 0; i--) {
        if (gGuiWindowManager.Windows[i].Closing) {
            continue;
        }
        if (gGuiWindowManager.Windows[i].Active && gGuiWindowManager.Windows[i].Kind == GUI_WIN_USER) {
            CloseWindow(i);
        }
    }
}

/*
 * 真机 SMP：点 × 在 Gui 核上关窗时，用户核可能已 exit。
 * 必须等到 Closing 清零再 Destroy（禁止自旋上限超时后强拆页表）。
 */
void GuiWaitNoWindowClosing(void) {
    UINTN Spin = 0;

    for (;;) {
        int i;
        int Busy = 0;

        for (i = 0; i < MAX_WINS; i++) {
            if (gGuiWindowManager.Windows[i].Closing) {
                Busy = 1;
                break;
            }
        }
        if (!Busy) {
            return;
        }
        if (Spin == 0) {
            DebugWrite("Gui: wait closing (smp)\n");
        }
        Spin++;
        if ((Spin & 0xFFFFFu) == 0) {
            DebugWrite("Gui: still closing...\n");
        }
        HalCpuRelax();
    }
}
