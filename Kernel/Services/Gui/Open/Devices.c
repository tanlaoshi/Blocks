/*
 * Devices.c — 设备管理器开窗（story-s1 / PR-DEV-6）
 *
 * 与 Apps.c 同链：Action→GuiOpenDevices→OpenChromeDefer→DevicesUiOpen→OpenFadeIn。
 * 单例：FocusExistingKind(DEVICES)。
 */
#include "GuiPrivate.h"
#include "Theme.h"
#include "DevicesUi.h"
#include "Locale.h"
#include "Debug.h"

int GuiOpenDevices(void) {
    int Idx;
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;
    UINT32 Margin = 40;

    Idx = FocusExistingKind(GUI_WIN_DEVICES, DevicesUiRepaint, "devices");
    if (Idx >= 0) {
        return Idx;
    }

    Idx = AllocWindowSlot();
    if (Idx < 0) {
        return -1;
    }
    W = 720;
    H = 520;
    if (W + Margin * 2 > gGuiWindowManager.ScreenWidth) {
        W = gGuiWindowManager.ScreenWidth > Margin * 2 ? gGuiWindowManager.ScreenWidth - Margin * 2 : gGuiWindowManager.ScreenWidth / 2;
    }
    if (H + Margin * 2 > gGuiWindowManager.ScreenHeight) {
        H = gGuiWindowManager.ScreenHeight > Margin * 2 ? gGuiWindowManager.ScreenHeight - Margin * 2 : gGuiWindowManager.ScreenHeight / 2;
    }
    X = Margin;
    Y = Margin + 24;
    gGuiWindowManager.Windows[Idx].Active = 1;
    gGuiWindowManager.Windows[Idx].Kind = GUI_WIN_DEVICES;
    gGuiWindowManager.Windows[Idx].X = X;
    gGuiWindowManager.Windows[Idx].Y = Y;
    gGuiWindowManager.Windows[Idx].Width = W;
    gGuiWindowManager.Windows[Idx].Height = H;
    gGuiWindowManager.Windows[Idx].Background = ThemeSettingsClientBackground();
    gGuiWindowManager.Windows[Idx].Title = LocStr(MSG_APP_DEVICES);
    gGuiWindowManager.Windows[Idx].TermSet = 0;
    gGuiWindowManager.Windows[Idx].InputLen = 0;
    gGuiWindowManager.Windows[Idx].WaitPrompt = 0;
    gGuiWindowManager.Windows[Idx].PromptShown = 0;
    gGuiWindowManager.Windows[Idx].InputLine[0] = 0;

    OpenChromeDefer(Idx);
    DevicesUiOpen();
    OpenFadeIn(Idx);
    DebugWrite("Gui: open devices idx=");
    DebugHex32((UINT32)gGuiWindowManager.FocusWin);
    DebugWrite("\n");
    return gGuiWindowManager.FocusWin;
}
