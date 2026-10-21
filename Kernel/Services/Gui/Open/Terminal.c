/*
 * Terminal.c — 串口会话窗开窗（PR-GUI-tty-win / S1.3）
 *
 * 槽位：InitWindowSlot / ClampWindowBox（Open.c）。
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
    ClampWindowBox(Margin, &W, &H);
    X = Margin + 24;
    Y = Margin + 48;
    InitWindowSlot(Idx, GUI_WIN_TTY, X, Y, W, H, ThemeShellClientBackground(),
                   LocStr(MSG_APP_TTY), 0);

    OpenChromeDefer(Idx);
    TtyUiOpen();
    OpenFadeIn(Idx);
    DebugWrite("Gui: open tty idx=");
    DebugHex32((UINT32)gGuiWindowManager.FocusWin);
    DebugWrite("\n");
    return gGuiWindowManager.FocusWin;
}
