/*
 * Devices.c — 设备管理器开窗（story-s1 / PR-DEV-6 / S1.3）
 *
 * 与 Apps.c 同链：Action→GuiOpenDevices→OpenChromeDefer→DevicesUiOpen→OpenFadeIn。
 * 槽位：InitWindowSlot / ClampWindowBox（Open.c）。单例 FocusExistingKind。
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
    ClampWindowBox(Margin, &W, &H);
    X = Margin;
    Y = Margin + 24;
    InitWindowSlot(Idx, GUI_WIN_DEVICES, X, Y, W, H, ThemeSettingsClientBackground(),
                   LocStr(MSG_APP_DEVICES), 0);

    OpenChromeDefer(Idx);
    DevicesUiOpen();
    OpenFadeIn(Idx);
    DebugWrite("Gui: open devices idx=");
    DebugHex32((UINT32)gGuiWindowManager.FocusWin);
    DebugWrite("\n");
    return gGuiWindowManager.FocusWin;
}
