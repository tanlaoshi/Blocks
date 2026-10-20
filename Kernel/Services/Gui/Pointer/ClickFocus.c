/*
 * GuiClickFocus.c — USER 命中与置顶后按窗种类分发（PR-F-guiclick-1）
 *
 * 从 GuiClick.c 抽出；不改点击语义。
 */
#include "GuiPrivate.h"
#include "HalVideo.h"
#include "Debug.h"
#include "SettingsUi.h"
#include "FilesUi.h"
#include "StoreUi.h"
#include "DevicesUi.h"
#include "EditUi.h"
#include "TtyUi.h"
#include "Console.h"

int GuiInputOwner(void) {
    if (gGuiWindowManager.DragWin >= 0) {
        return gGuiWindowManager.DragWin;
    }
    return gGuiWindowManager.FocusWin;
}

int GuiClickTryUserClient(UINT32 X, UINT32 Y) {
    int i;
    int Hit;

    /*
     * USER 按钮：在 Raise/Sync 之前命中顶层窗。
     * SyncWindowVisuals 会重贴备份；Raise 会搬槽，导致之后命中失败或事件写到错槽。
     */
    for (i = MAX_WINS - 1; i >= 0; i--) {
        if (!PointInWindow(&gGuiWindowManager.Windows[i], X, Y)) {
            continue;
        }
        if (gGuiWindowManager.Windows[i].Kind == GUI_WIN_USER && !PointInTitle(&gGuiWindowManager.Windows[i], X, Y)) {
            if (PointInResizeCorner(&gGuiWindowManager.Windows[i], X, Y)) {
                break;
            }
            Hit = UserButtonHit(i, X, Y);
            if (Hit >= 0) {
                GuiFocusSave();
                RaiseWindow(i);
                GuiFocusApply();
                gGuiWindowManager.InputOwner = gGuiWindowManager.FocusWin;
                gGuiWindowManager.InputButton = Hit;
                gGuiWindowManager.InputClient = 0;
                return 1;
            }
            /* 客户区：记下点击。已在最前时勿 GuiRaiseToFront：
             * DrawWindowAtEx/PaintUserClient 会整块刷底，抹掉 ToyGfx 复选框。 */
            {
                UINT32 Cx = gGuiWindowManager.Windows[i].X + 1 + GUI_CLIENT_PAD;
                UINT32 Cy = gGuiWindowManager.Windows[i].Y + TITLE_HEIGHT + GUI_CLIENT_PAD;
                if (X >= Cx && Y >= Cy) {
                    UINT32 RelX = X - Cx;
                    UINT32 RelY = Y - Cy;

                    if (i != gGuiWindowManager.FocusWin) {
                        GuiFocusSave();
                        GuiRaiseToFront(i);
                    }
                    gGuiWindowManager.InputOwner = gGuiWindowManager.FocusWin;
                    gGuiWindowManager.InputClient = 1;
                    gGuiWindowManager.InputClickX = RelX;
                    gGuiWindowManager.InputClickY = RelY;
                    gGuiWindowManager.InputButton = -1;
                    return 1;
                }
            }
        }
        break; /* 顶层命中窗不是按钮，走下方通用逻辑 */
    }
    return 0;
}

static void GuiClickDispatchKind(UINT32 X, UINT32 Y) {
    if (GuiFocusKind() == GUI_WIN_SETTINGS) {
        if (PointInTitle(&gGuiWindowManager.Windows[gGuiWindowManager.FocusWin], X, Y)) {
            SettingsUiRepaint();
            /* 客户区重绘后强制刷新标题，清光标/透视残块 */
            GuiFrameBufferBegin();
            DrawWindowChromeAt(gGuiWindowManager.FocusWin);
            GuiFrameBufferEnd();
        }
        /* 客户区：按下高亮由 OnPointer；抬起才触发（见 SettingsUiOnPointer / StoreUiOnPointer） */
    } else if (GuiFocusKind() == GUI_WIN_STORE) {
        if (PointInTitle(&gGuiWindowManager.Windows[gGuiWindowManager.FocusWin], X, Y)) {
            StoreUiRepaint();
            GuiFrameBufferBegin();
            DrawWindowChromeAt(gGuiWindowManager.FocusWin);
            GuiFrameBufferEnd();
        }
        /* 客户区：同 Settings，不在按下时 OnClick */
    } else if (GuiFocusKind() == GUI_WIN_DEVICES) {
        if (PointInTitle(&gGuiWindowManager.Windows[gGuiWindowManager.FocusWin], X, Y)) {
            DevicesUiRepaint();
            GuiFrameBufferBegin();
            DrawWindowChromeAt(gGuiWindowManager.FocusWin);
            GuiFrameBufferEnd();
        } else {
            DevicesUiOnClick(X, Y);
        }
    } else if (GuiFocusKind() == GUI_WIN_FILES) {
        if (PointInTitle(&gGuiWindowManager.Windows[gGuiWindowManager.FocusWin], X, Y)) {
            FilesUiRepaint();
            GuiFrameBufferBegin();
            DrawWindowChromeAt(gGuiWindowManager.FocusWin);
            GuiFrameBufferEnd();
        } else {
            FilesUiOnClick(X, Y);
        }
    } else if (GuiFocusKind() == GUI_WIN_EDIT) {
        if (PointInTitle(&gGuiWindowManager.Windows[gGuiWindowManager.FocusWin], X, Y)) {
            EditUiRepaint();
            GuiFrameBufferBegin();
            DrawWindowChromeAt(gGuiWindowManager.FocusWin);
            GuiFrameBufferEnd();
        }
        /* 客户区：悬停/按下由 OnPointer；抬起触发 Save（PR-GUI-migrate-edit） */
    } else if (GuiFocusKind() == GUI_WIN_TTY) {
        if (PointInTitle(&gGuiWindowManager.Windows[gGuiWindowManager.FocusWin], X, Y)) {
            TtyUiRepaint();
            GuiFrameBufferBegin();
            DrawWindowChromeAt(gGuiWindowManager.FocusWin);
            GuiFrameBufferEnd();
        } else {
            TtyUiOnClick(X, Y);
        }
    } else if (GuiFocusKind() == GUI_WIN_SHELL) {
        if (PointInTitle(&gGuiWindowManager.Windows[gGuiWindowManager.FocusWin], X, Y)) {
            if (!gGuiWindowManager.WinBackupValid[gGuiWindowManager.FocusWin]) {
                GuiConsoleOpsOnShellOpened();
            }
        } else {
            gConsole.OnClick(X, Y);
            if (!gGuiWindowManager.WinBackupValid[gGuiWindowManager.FocusWin]) {
                GuiConsoleOpsOnShellOpened();
            }
        }
    } else if (gGuiWindowManager.FocusWin >= 0) {
        BackupWindowAt(gGuiWindowManager.FocusWin);
    }
}

int GuiClickTryRaiseWindow(UINT32 X, UINT32 Y) {
    int i;

    for (i = MAX_WINS - 1; i >= 0; i--) {
        if (!PointInWindow(&gGuiWindowManager.Windows[i], X, Y)) {
            continue;
        }
        GuiFocusSave();
        RaiseWindow(i);
        SyncWindowVisuals();
        GuiFocusApply();
        DebugWrite("Gui: focus ");
        DebugWrite(gGuiWindowManager.Windows[gGuiWindowManager.FocusWin].Title);
        DebugWrite("\n");

        if (PointInResizeCorner(&gGuiWindowManager.Windows[gGuiWindowManager.FocusWin], X, Y) &&
            !PointOnAnyClose(X, Y)) {
            GraphicsIrqEnter();
            CursorRestore();
            GraphicsIrqLeave();
            RaiseWindow(gGuiWindowManager.FocusWin);
            GuiResizeBegin(gGuiWindowManager.FocusWin, X, Y);
            /* 按下瞬间须立刻换成 E/S/SE 形，勿等下一次 Move */
            GuiCursorPaint();
            return 1;
        }
        if (PointInTitle(&gGuiWindowManager.Windows[gGuiWindowManager.FocusWin], X, Y) &&
            !PointOnAnyClose(X, Y)) {
            GraphicsIrqEnter();
            CursorRestore();
            GraphicsIrqLeave();
            RaiseWindow(gGuiWindowManager.FocusWin);
            gGuiWindowManager.ResizeWin = -1;
            gGuiWindowManager.DragWin = gGuiWindowManager.FocusWin;
            gGuiWindowManager.DragOffX = (INT32)X - (INT32)gGuiWindowManager.Windows[gGuiWindowManager.FocusWin].X;
            gGuiWindowManager.DragOffY = (INT32)Y - (INT32)gGuiWindowManager.Windows[gGuiWindowManager.FocusWin].Y;
            gGuiWindowManager.DragArmed = 1;
        }
        GuiClickDispatchKind(X, Y);
        return 1;
    }
    return 0;
}
