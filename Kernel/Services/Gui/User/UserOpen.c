/*
 * UserOpen.c — GuiOpenUser（自 User.c 纯搬 · PR-NORM-16）
 */
#include "GuiPrivate.h"
#include "UI.h"
#include "HalVideo.h"
#include "Hal.h"
#include "Debug.h"
#include "Theme.h"
#include "Font.h"

int TitleIsFixedUser(const char *Title);

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
    FontTtfPreheatUtf8(Title);
    CopyTitleBuffer(gGuiWindowManager.Windows[Idx].TitleBuf, sizeof(gGuiWindowManager.Windows[Idx].TitleBuf), Title);
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

    gGuiWindowManager.FocusWin = Idx;
    RaiseWindow(Idx);
    Idx = gGuiWindowManager.FocusWin;
    gGuiWindowManager.WinBackupValid[Idx] = 0;
    /*
     * 只画本窗 + Present（非 Flush）。禁 SyncWindowVisualsEx 全桌：
     * Chat 等一开窗就 PresentFlush → PresentBlocked 饿死 xHCI。
     * 与 RepaintUserWindow 同契约。
     */
    GuiInputLock(1);
    ComposeBeginEraseCursor();
    HalVideoClearClip();
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
    HalVideoPresent();
    ComposeEnd();
    GuiInputLock(0);
    DebugWrite("Gui: open user idx=");
    DebugHex32((UINT32)gGuiWindowManager.FocusWin);
    DebugWrite("\n");
    return gGuiWindowManager.FocusWin;
}
