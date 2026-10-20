/*
 * Damage.c — Gui 合成脏矩形（gui-g0-map … gui-g5-fallback）
 *
 * GuiMarkDamage：并一个屏幕 AABB（仅 Gui 层；≠ HAL DirtyUnion）。
 * GuiComposeDamage（gui-g5-fallback 回退 → ThemeScene）：
 *   GuiDragActive → 早退（gui-g8-drag：拖/改大小中合成柱勿画；Drag 自管）
 *   无标记 → ThemeScene
 *   裁剪后空矩形 → 早退（无事可画）
 *   并集面积 ≥ 屏 50% → ThemeScene（大洞脏路径不比全屏省）
 *   命中活动窗 ≥ 3 → ThemeScene（多窗 PaintOne 易比全屏贵）
 *   否则 → 擦→DesktopDrawInRect→命中窗→菜单/托盘→光标
 *            → PresentFlush（ComposeBusy 内）→ ComposeEnd
 *
 * 调用面清单：
 *   GuiMarkDamage —
 *     Clock.c（图标 bbox；TTF 时 0,0,Sw,Sh → 面积回退全屏）
 *     Desktop/Mark.c（栏 / 开始菜单 cover / 右键 cover）
 *     NetTrayPopup DesktopMarkNetTrayPopup
 *   RequestRefresh → GuiRefreshDesktop — 菜单/右键/网托快路径见 Refresh.c；
 *     否则本函数（G4 Mark + G5 回退）
 *   直 ThemeScene — Mouse 改分辨率、Theme、Catalog、Close 非 USER
 *   轻量自画 — Close USER（禁 ThemeScene；Close.c 头 gui-g3-close）
 * 总图 Compose.c。
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

/* 并集占屏比（百分）；≥ 此值走 ThemeScene */
#define DAMAGE_FALLBACK_AREA_PCT 50u
/* 脏矩形命中活动窗数；≥ 此值走 ThemeScene */
#define DAMAGE_FALLBACK_WIN_HITS 3

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

static int CountWindowHits(UINT32 X, UINT32 Y, UINT32 W, UINT32 H) {
    int i;
    int N = 0;

    for (i = 0; i < MAX_WINS; i++) {
        if (!gGuiWindowManager.Windows[i].Active) {
            continue;
        }
        if (WindowHits(i, X, Y, W, H)) {
            N++;
        }
    }
    return N;
}

static int AreaNeedsFullscreen(UINT32 W, UINT32 H) {
    UINT32 Sw;
    UINT32 Sh;
    UINT64 Dirty;
    UINT64 Screen;

    HalVideoGetSize(&Sw, &Sh);
    if (Sw == 0 || Sh == 0) {
        return 1;
    }
    Dirty = (UINT64)W * (UINT64)H;
    Screen = (UINT64)Sw * (UINT64)Sh;
    /* Dirty/Screen ≥ PCT/100  ↔  Dirty*100 ≥ Screen*PCT */
    return (Dirty * (UINT64)100 >= Screen * (UINT64)DAMAGE_FALLBACK_AREA_PCT) ? 1 : 0;
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
    int Hits;

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
    if (AreaNeedsFullscreen(W, H)) {
        GuiComposeThemeScene();
        return;
    }
    Hits = CountWindowHits(X, Y, W, H);
    if (Hits >= DAMAGE_FALLBACK_WIN_HITS) {
        GuiComposeThemeScene();
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
