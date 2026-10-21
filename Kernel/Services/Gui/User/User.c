/*
 * GuiUser.c — 用户态窗口协议（核心）
 * 辅助：GuiUserBlit.c
 *
 * 从 GuiUser.c 单体迁出；只搬家、不改逻辑。
 */
#include "GuiPrivate.h"
#include "UI.h"
#include "HalVideo.h"
#include "Hal.h"
#include "Debug.h"
#include "Theme.h"
#include "Font.h"

void CopyTitleBuf(char *Dst, UINTN Cap, const char *Src) {
    UINTN i;

    if (!Dst || Cap == 0) {
        return;
    }
    if (!Src) {
        Dst[0] = 0;
        return;
    }
    for (i = 0; i + 1 < Cap && Src[i]; i++) {
        Dst[i] = Src[i];
    }
    Dst[i] = 0;
}

/*
 * Raise 挪槽后，用户态仍握着旧 wid。先认仍有效的 USER 槽；
 * 否则若仅一扇 USER 则跟它；多扇时跟焦点 USER。
 */
int ResolveUserWindowIndex(int Wid) {
    int i;
    int Found = -1;
    int N = 0;

    if (Wid >= 0 && Wid < MAX_WINS && gGuiWindowManager.Windows[Wid].Active &&
        gGuiWindowManager.Windows[Wid].Kind == GUI_WIN_USER) {
        return Wid;
    }
    for (i = 0; i < MAX_WINS; i++) {
        if (gGuiWindowManager.Windows[i].Active && gGuiWindowManager.Windows[i].Kind == GUI_WIN_USER) {
            Found = i;
            N++;
        }
    }
    if (N == 1) {
        return Found;
    }
    if (gGuiWindowManager.FocusWin >= 0 && gGuiWindowManager.FocusWin < MAX_WINS &&
        gGuiWindowManager.Windows[gGuiWindowManager.FocusWin].Active &&
        gGuiWindowManager.Windows[gGuiWindowManager.FocusWin].Kind == GUI_WIN_USER) {
        return gGuiWindowManager.FocusWin;
    }
    return Found;
}

/* Raise 后槽位可能移动；返回当前 USER 窗下标，失败 -1 */
int UserWindowIndexAfterRaise(int Wid) {
    int Idx;

    Idx = ResolveUserWindowIndex(Wid);
    if (Idx < 0) {
        return -1;
    }
    RaiseWindow(Idx);
    if (gGuiWindowManager.FocusWin >= 0 && gGuiWindowManager.FocusWin < MAX_WINS &&
        gGuiWindowManager.Windows[gGuiWindowManager.FocusWin].Active && gGuiWindowManager.Windows[gGuiWindowManager.FocusWin].Kind == GUI_WIN_USER) {
        return gGuiWindowManager.FocusWin;
    }
    return -1;
}


/*
 * 不透明重画用户窗客户区（标签/按钮）。
 * 勿 SyncWindowVisualsEx 全桌：ComposeBusy+PresentFlush 整屏会饿死 xHCI → 点 OK 后鼠死。
 */
void RepaintUserWindow(int Wid) {
    int Idx;

    /* 开窗/加按钮期间禁鼠标边沿，避免误点进 Poll */
    GuiInputLock(1);
    Idx = UserWindowIndexAfterRaise(Wid);
    if (Idx < 0) {
        GuiInputLock(0);
        return;
    }
    ComposeBeginEraseCursor();
    HalVideoClearClip();
    /*
     * 只重画本窗 chrome+客户区。勿 Sync 全桌：ComposeBusy+PresentFlush
     * 整屏会饿死 xHCI（GuiDemo 点 OK 后鼠只闪不动）。
     */
    GraphicsIrqEnter();
    CursorRestore();
    GraphicsIrqLeave();
    DrawWindowAtEx(Idx, 0);
    PaintUserClient(Idx);
    BackupWindowAtEx(Idx, 1);
    GuiFocusApply();
    GraphicsIrqEnter();
    CursorPaint();
    GraphicsIrqLeave();
    /* 一窗脏即可；Flush 整屏会长时间 PresentBlocked */
    HalVideoPresent();
    ComposeEnd();
    gGuiWindowManager.UserSlot[Idx].UserButtonClick = -1;
    gGuiWindowManager.UserSlot[Idx].UserClientClick = 0;
    gGuiWindowManager.UserSlot[Idx].UserKeyCount = 0;
    GuiInputLock(0);
}


int UserButtonHit(int Idx, UINT32 X, UINT32 Y) {
    GUI_WINDOW *W;
    UINT32 Cx;
    UINT32 Cy;
    UINT32 Cw;
    UINT32 Ch;
    UINT32 Bx;
    UINT32 By;
    UINT32 Bw;
    UINT32 Bh;
    UINT32 Gap = 8;
    int Bi;
    int Count;
    int Slot;

    if (Idx < 0 || Idx >= MAX_WINS) {
        return -1;
    }
    W = &gGuiWindowManager.Windows[Idx];
    if (!W->Active || W->Kind != GUI_WIN_USER) {
        return -1;
    }
    Cx = W->X + 1 + GUI_CLIENT_PAD;
    Cy = W->Y + TITLE_HEIGHT + GUI_CLIENT_PAD;
    Cw = W->Width - 2 - GUI_CLIENT_PAD * 2;
    Ch = W->Height - TITLE_HEIGHT - 1 - GUI_CLIENT_PAD * 2;
    if (Ch < 48) {
        return -1;
    }
    Count = 0;
    for (Bi = 0; Bi < 4; Bi++) {
        if (gGuiWindowManager.UserSlot[Idx].UserButtonUsed[Bi]) {
            Count++;
        }
    }
    if (Count == 0) {
        return -1;
    }
    Bh = 36;
    Bw = (Cw - Gap * (Count + 1)) / (UINT32)Count;
    if (Bw < 64) {
        Bw = 64;
    }
    if (Bw > 160) {
        Bw = 160;
    }
    By = Cy + Ch - Bh - 4;
    Bx = Cx + Gap;
    Slot = 0;
    for (Bi = 0; Bi < 4; Bi++) {
        UINT32 Left;
        UINT32 Right;

        if (!gGuiWindowManager.UserSlot[Idx].UserButtonUsed[Bi]) {
            continue;
        }
        Left = Bx + Slot * (Bw + Gap);
        Right = Left + Bw;
        if (X >= Left && X < Right && Y >= By && Y < By + Bh) {
            return Bi;
        }
        Slot++;
    }
    return -1;
}


/* 固定布局 USER 窗：不可拖边改大小（Snake 棋盘 / TaskMgr 标签） */
static int TitleEq(const char *Title, const char *Name) {
    int i;

    if (!Title || !Name) {
        return 0;
    }
    for (i = 0; Name[i] != 0; i++) {
        if (Title[i] != Name[i]) {
            return 0;
        }
    }
    return Title[i] == 0;
}

static int TitleIsFixedUser(const char *Title) {
    return TitleEq(Title, "Snake") || TitleEq(Title, "Task Manager") ||
           TitleEq(Title, "任务管理器");
}

int GuiOpenUser(const char *Title, UINT32 W, UINT32 H) {
    int Idx;
    UINT32 X;
    UINT32 Y;
    UINT32 Margin = 48;

    Idx = AllocWindowSlot();
    if (Idx < 0) {
        return -1;
    }
    if (W < 160) {
        W = 160;
    }
    if (H < 100) {
        H = 100;
    }
    if (W + Margin * 2 > gGuiWindowManager.ScreenWidth) {
        W = gGuiWindowManager.ScreenWidth > Margin * 2 ? gGuiWindowManager.ScreenWidth - Margin * 2 : gGuiWindowManager.ScreenWidth / 2;
    }
    if (H + Margin * 2 > gGuiWindowManager.ScreenHeight) {
        H = gGuiWindowManager.ScreenHeight > Margin * 2 ? gGuiWindowManager.ScreenHeight - Margin * 2 : gGuiWindowManager.ScreenHeight / 2;
    }
    X = (gGuiWindowManager.ScreenWidth > W) ? (gGuiWindowManager.ScreenWidth - W) / 2 : 0;
    Y = (gGuiWindowManager.ScreenHeight > H + 40) ? (gGuiWindowManager.ScreenHeight - H) / 3 : Margin;

    gGuiWindowManager.Windows[Idx].Active = 1;
    gGuiWindowManager.Windows[Idx].FixedSize = TitleIsFixedUser(Title);
    gGuiWindowManager.Windows[Idx].Kind = GUI_WIN_USER;
    gGuiWindowManager.Windows[Idx].X = X;
    gGuiWindowManager.Windows[Idx].Y = Y;
    gGuiWindowManager.Windows[Idx].Width = W;
    gGuiWindowManager.Windows[Idx].Height = H;
    gGuiWindowManager.Windows[Idx].Background = ThemeSettingsClientBackground();
    CopyTitleBuf(gGuiWindowManager.Windows[Idx].TitleBuf, sizeof(gGuiWindowManager.Windows[Idx].TitleBuf), Title);
    gGuiWindowManager.Windows[Idx].Title = gGuiWindowManager.Windows[Idx].TitleBuf;
    gGuiWindowManager.Windows[Idx].ClientText[0] = 0;
    gGuiWindowManager.Windows[Idx].ClosePending = 0;
    gGuiWindowManager.Windows[Idx].Closing = 0;
    gGuiWindowManager.UserSlot[Idx].UserButtonClick = -1;
    gGuiWindowManager.UserSlot[Idx].UserClientClick = 0;
    gGuiWindowManager.UserSlot[Idx].UserClickX = 0;
    gGuiWindowManager.UserSlot[Idx].UserClickY = 0;
    gGuiWindowManager.UserSlot[Idx].UserKeyCount = 0;
    {
        int Bi;
        for (Bi = 0; Bi < 4; Bi++) {
            gGuiWindowManager.UserSlot[Idx].UserButtonUsed[Bi] = 0;
            gGuiWindowManager.UserSlot[Idx].UserButtonLabel[Bi][0] = 0;
        }
    }
    gGuiWindowManager.Windows[Idx].TermSet = 0;
    gGuiWindowManager.Windows[Idx].InputLen = 0;
    gGuiWindowManager.Windows[Idx].WaitPrompt = 0;
    gGuiWindowManager.Windows[Idx].PromptShown = 0;
    gGuiWindowManager.Windows[Idx].InputLine[0] = 0;

    ComposeBeginEraseCursor();
    HalVideoClearClip();
    DrawWindowAtEx(Idx, 0);
    ComposeEnd();
    gGuiWindowManager.FocusWin = Idx;
    RaiseWindow(Idx);
    Idx = gGuiWindowManager.FocusWin;
    gGuiWindowManager.WinBackupValid[Idx] = 0;
    ComposeBeginEraseCursor();
    SyncWindowVisualsEx(0);
    GraphicsIrqEnter();
    CursorRestore();
    GraphicsIrqLeave();
    DrawWindowAtEx(Idx, 0);
    PaintUserClient(Idx);
    BackupWindowAtEx(Idx, 1);
    GuiFocusApply();
    GraphicsIrqEnter();
    CursorPaint();
    GraphicsIrqLeave();
    HalVideoPresentFlush();
    ComposeEnd();
    DebugWrite("Gui: open user idx=");
    DebugHex32((UINT32)gGuiWindowManager.FocusWin);
    DebugWrite("\n");
    return gGuiWindowManager.FocusWin;
}

int GuiUserAddButton(int Wid, int ButtonId, const char *Label) {
    int Idx;

    Idx = ResolveUserWindowIndex(Wid);
    if (Idx < 0 || ButtonId < 0 || ButtonId >= 4 || !Label) {
        return -1;
    }
    gGuiWindowManager.UserSlot[Idx].UserButtonUsed[ButtonId] = 1;
    CopyTitleBuf(gGuiWindowManager.UserSlot[Idx].UserButtonLabel[ButtonId],
                 sizeof(gGuiWindowManager.UserSlot[Idx].UserButtonLabel[ButtonId]), Label);
    RepaintUserWindow(Idx);
    return 0;
}
