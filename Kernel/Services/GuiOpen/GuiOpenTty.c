/*
 * GuiOpenTty.c — 串口会话窗开窗（PR-GUI-tty-win）
 */
#include "GuiPrivate.h"
#include "Theme.h"
#include "TtyUi.h"
#include "Locale.h"
#include "Debug.h"

int GuiOpenTty(void) {
    int Idx;
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;
    UINT32 Margin = 48;

    Idx = FocusExistingKind(GUI_WIN_TTY, TtyUiRepaint, "tty");
    if (Idx >= 0) {
        return Idx;
    }

    Idx = AllocWindowSlot();
    if (Idx < 0) {
        return -1;
    }
    W = 720;
    H = 480;
    if (W + Margin * 2 > gGuiWindowManager.ScreenWidth) {
        W = gGuiWindowManager.ScreenWidth > Margin * 2 ? gGuiWindowManager.ScreenWidth - Margin * 2 : gGuiWindowManager.ScreenWidth / 2;
    }
    if (H + Margin * 2 > gGuiWindowManager.ScreenHeight) {
        H = gGuiWindowManager.ScreenHeight > Margin * 2 ? gGuiWindowManager.ScreenHeight - Margin * 2 : gGuiWindowManager.ScreenHeight / 2;
    }
    X = Margin + 24;
    Y = Margin + 48;
    gGuiWindowManager.Windows[Idx].Active = 1;
    gGuiWindowManager.Windows[Idx].Kind = GUI_WIN_TTY;
    gGuiWindowManager.Windows[Idx].X = X;
    gGuiWindowManager.Windows[Idx].Y = Y;
    gGuiWindowManager.Windows[Idx].Width = W;
    gGuiWindowManager.Windows[Idx].Height = H;
    gGuiWindowManager.Windows[Idx].Background = ThemeShellClientBackground();
    gGuiWindowManager.Windows[Idx].Title = LocStr(MSG_APP_TTY);
    gGuiWindowManager.Windows[Idx].TermSet = 0;
    gGuiWindowManager.Windows[Idx].InputLen = 0;
    gGuiWindowManager.Windows[Idx].WaitPrompt = 0;
    gGuiWindowManager.Windows[Idx].PromptShown = 0;
    gGuiWindowManager.Windows[Idx].InputLine[0] = 0;

    OpenChromeDefer(Idx);
    TtyUiOpen();
    OpenFadeIn(Idx);
    DebugWrite("Gui: open tty idx=");
    DebugHex32((UINT32)gGuiWindowManager.FocusWin);
    DebugWrite("\n");
    return gGuiWindowManager.FocusWin;
}
