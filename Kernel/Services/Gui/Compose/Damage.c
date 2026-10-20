/*
 * Damage.c — Gui 合成脏矩形（gui-g0-map）
 *
 * GuiMarkDamage：并一个屏幕 AABB（仅 Gui 层；≠ HAL DirtyUnion）。
 * GuiComposeDamage：
 *   拖动中 → 早退（Drag 自管备份/PresentBlocked）
 *   无标记 → GuiComposeThemeScene（整屏回退）
 *   有矩形 → 擦→DesktopDrawInRect→命中窗→菜单/托盘→光标
 *            → PresentFlush（ComposeBusy 内）→ ComposeEnd
 * 总图见 Compose.c 头；菜单快路径见 Refresh.c。
 */
#include "GuiPrivate.h"
#include "HalVideo.h"
#include "Desktop.h"
#include "SettingsUi.h"
#include "FilesUi.h"
#include "StoreUi.h"
#include "DevicesUi.h"
#include "EditUi.h"
#include "TtyUi.h"

static int gHave;
static UINT32 gX;
static UINT32 gY;
static UINT32 gW;
static UINT32 gH;

static int Overlap(UINT32 Ax, UINT32 Ay, UINT32 Aw, UINT32 Ah,
                   UINT32 Bx, UINT32 By, UINT32 Bw, UINT32 Bh) {
    if (Aw == 0 || Ah == 0 || Bw == 0 || Bh == 0) {
        return 0;
    }
    return Ax < Bx + Bw && Bx < Ax + Aw && Ay < By + Bh && By < Ay + Ah;
}

void GuiMarkDamage(UINT32 X, UINT32 Y, UINT32 W, UINT32 H) {
    UINT32 X1;
    UINT32 Y1;

    if (W == 0 || H == 0) {
        return;
    }
    if (!gHave) {
        gX = X;
        gY = Y;
        gW = W;
        gH = H;
        gHave = 1;
        return;
    }
    X1 = gX + gW;
    Y1 = gY + gH;
    if (X < gX) {
        gX = X;
    }
    if (Y < gY) {
        gY = Y;
    }
    if (X + W > X1) {
        X1 = X + W;
    }
    if (Y + H > Y1) {
        Y1 = Y + H;
    }
    gW = X1 - gX;
    gH = Y1 - gY;
}

static int WindowHits(int Idx, UINT32 X, UINT32 Y, UINT32 W, UINT32 H) {
    GUI_WINDOW *Win;
    UINT32 Wx;
    UINT32 Wy;
    UINT32 Ww;
    UINT32 Wh;

    Win = &gGuiWindowManager.Windows[Idx];
    Wx = Win->X;
    Wy = Win->Y;
    Ww = Win->Width;
    Wh = Win->Height;
    ExpandRectByWindowShadow(&Wx, &Wy, &Ww, &Wh);
    return Overlap(Wx, Wy, Ww, Wh, X, Y, W, H);
}

static void PaintOne(int Idx, int SavedFocus) {
    gGuiWindowManager.FocusWin = SavedFocus;
    DrawWindowAtEx(Idx, 0);
    if (gGuiWindowManager.Windows[Idx].Kind == GUI_WIN_SHELL) {
        gGuiWindowManager.FocusWin = Idx;
        GuiConsoleOpsPaintShellWindow(Idx);
    } else if (gGuiWindowManager.Windows[Idx].Kind == GUI_WIN_SETTINGS) {
        gGuiWindowManager.FocusWin = Idx;
        SettingsUiPaintFocused();
    } else if (gGuiWindowManager.Windows[Idx].Kind == GUI_WIN_STORE) {
        gGuiWindowManager.FocusWin = Idx;
        StoreUiPaintFocused();
    } else if (gGuiWindowManager.Windows[Idx].Kind == GUI_WIN_DEVICES) {
        gGuiWindowManager.FocusWin = Idx;
        DevicesUiPaintFocused();
    } else if (gGuiWindowManager.Windows[Idx].Kind == GUI_WIN_FILES) {
        gGuiWindowManager.FocusWin = Idx;
        FilesUiPaintFocused();
    } else if (gGuiWindowManager.Windows[Idx].Kind == GUI_WIN_EDIT) {
        gGuiWindowManager.FocusWin = Idx;
        EditUiPaintFocused();
    } else if (gGuiWindowManager.Windows[Idx].Kind == GUI_WIN_TTY) {
        gGuiWindowManager.FocusWin = Idx;
        TtyUiPaintFocused();
    } else if (gGuiWindowManager.Windows[Idx].Kind == GUI_WIN_USER) {
        gGuiWindowManager.FocusWin = Idx;
        PaintUserClient(Idx);
    }
    BackupWindowAtEx(Idx, 1);
}

void GuiComposeDamage(void) {
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;
    int i;
    int SavedFocus;

    if (GuiDragActive()) {
        return;
    }
    if (!gHave) {
        GuiComposeThemeScene();
        return;
    }
    X = gX;
    Y = gY;
    W = gW;
    H = gH;
    gHave = 0;
    ClipRectToScreen(&X, &Y, &W, &H);
    if (W == 0 || H == 0) {
        return;
    }

    SavedFocus = gGuiWindowManager.FocusWin;
    gGuiWindowManager.DeferPresent = 1;
    ComposeBeginEraseCursor();
    HalVideoClearClip();
    DesktopFillRect(X, Y, W, H);
    DesktopDrawInRect(X, Y, W, H);

    for (i = 0; i < MAX_WINS; i++) {
        if (!gGuiWindowManager.Windows[i].Active || !WindowHits(i, X, Y, W, H)) {
            continue;
        }
        PaintOne(i, SavedFocus);
    }
    for (i = 0; i < MAX_WINS; i++) {
        if (gGuiWindowManager.Windows[i].Active && WindowHits(i, X, Y, W, H)) {
            DrawWindowShadowAt(i);
        }
    }

    HalVideoClearClip();
    DesktopDrawStartMenu();
    DesktopDrawContextMenu();
    DesktopDrawNetTrayPopup();
    gGuiWindowManager.FocusWin = SavedFocus;
    if (SavedFocus >= 0 && SavedFocus < MAX_WINS &&
        gGuiWindowManager.Windows[SavedFocus].Active) {
        GuiFocusApply();
    } else {
        HalVideoClearClip();
    }
    GraphicsIrqEnter();
    CursorPaint();
    GraphicsIrqLeave();
    gGuiWindowManager.DeferPresent = 0;
    HalVideoPresentFlush();
    ComposeEnd();
}
