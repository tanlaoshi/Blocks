/*
 * GuiOpenApps.c — Shell / Settings / Store / Files / Edit 开窗
 * 核心：GuiOpen.c
 */
#include "GuiPrivate.h"
#include "UI.h"
#include "HalVideo.h"
#include "Hal.h"
#include "Font.h"
#include "Debug.h"
#include "Theme.h"
#include "SettingsUi.h"
#include "FilesUi.h"
#include "StoreUi.h"
#include "EditUi.h"
#include "Locale.h"

int GuiOpenShell(void) {
    int Idx;
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;

    Idx = AllocWindowSlot();
    if (Idx < 0) {
        return -1;
    }
    PlaceNewWindow(Idx, &X, &Y, &W, &H);
    gGuiWindowManager.Windows[Idx].Active = 1;
    gGuiWindowManager.Windows[Idx].Kind = GUI_WIN_SHELL;
    gGuiWindowManager.Windows[Idx].X = X;
    gGuiWindowManager.Windows[Idx].Y = Y;
    gGuiWindowManager.Windows[Idx].Width = W;
    gGuiWindowManager.Windows[Idx].Height = H;
    gGuiWindowManager.Windows[Idx].Background = ThemeShellClientBackground();
    gGuiWindowManager.Windows[Idx].Title = LocStr(MSG_APP_SHELL);
    gGuiWindowManager.Windows[Idx].TermSet = 0;
    gGuiWindowManager.Windows[Idx].InputLen = 0;
    gGuiWindowManager.Windows[Idx].WaitPrompt = 0;
    /* 先标已 prompt，避免 FocusApply→FocusLoad 抢画；下方 OnShellOpened 再画欢迎语 */
    gGuiWindowManager.Windows[Idx].PromptShown = 1;
    gGuiWindowManager.Windows[Idx].InputLine[0] = 0;

    OpenChromeDefer(Idx);
    GuiFocusSave();
    GuiConsoleOpsOnShellOpened();
    OpenFadeIn(Idx);
    DebugWrite("Gui: open shell idx=");
    DebugHex32((UINT32)gGuiWindowManager.FocusWin);
    DebugWrite("\n");
    return gGuiWindowManager.FocusWin;
}


int GuiOpenSettings(void) {
    int Idx;
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;
    UINT32 Margin = 48;

    Idx = FocusExistingKind(GUI_WIN_SETTINGS, SettingsUiRepaint, "settings");
    if (Idx >= 0) {
        return Idx;
    }

    Idx = AllocWindowSlot();
    if (Idx < 0) {
        return -1;
    }
    /* 三分栏：与 Files 同量级，靠右 */
    W = 800;
    H = 560;
    if (W + Margin * 2 > gGuiWindowManager.ScreenWidth) {
        W = gGuiWindowManager.ScreenWidth > Margin * 2 ? gGuiWindowManager.ScreenWidth - Margin * 2 : gGuiWindowManager.ScreenWidth / 2;
    }
    if (H + Margin * 2 > gGuiWindowManager.ScreenHeight) {
        H = gGuiWindowManager.ScreenHeight > Margin * 2 ? gGuiWindowManager.ScreenHeight - Margin * 2 : gGuiWindowManager.ScreenHeight / 2;
    }
    X = (gGuiWindowManager.ScreenWidth > W + Margin) ? (gGuiWindowManager.ScreenWidth - W - Margin) : Margin;
    Y = Margin;
    gGuiWindowManager.Windows[Idx].Active = 1;
    gGuiWindowManager.Windows[Idx].Kind = GUI_WIN_SETTINGS;
    gGuiWindowManager.Windows[Idx].X = X;
    gGuiWindowManager.Windows[Idx].Y = Y;
    gGuiWindowManager.Windows[Idx].Width = W;
    gGuiWindowManager.Windows[Idx].Height = H;
    gGuiWindowManager.Windows[Idx].Background = ThemeSettingsClientBackground();
    gGuiWindowManager.Windows[Idx].Title = LocStr(MSG_APP_SETTINGS);
    gGuiWindowManager.Windows[Idx].TermSet = 0;
    gGuiWindowManager.Windows[Idx].InputLen = 0;
    gGuiWindowManager.Windows[Idx].WaitPrompt = 0;
    gGuiWindowManager.Windows[Idx].PromptShown = 0;
    gGuiWindowManager.Windows[Idx].InputLine[0] = 0;

    OpenChromeDefer(Idx);
    SettingsUiOpen();
    OpenFadeIn(Idx);
    DebugWrite("Gui: open settings idx=");
    DebugHex32((UINT32)gGuiWindowManager.FocusWin);
    DebugWrite("\n");
    return gGuiWindowManager.FocusWin;
}


int GuiOpenStore(void) {
    int Idx;
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;
    UINT32 Margin = 40;

    Idx = FocusExistingKind(GUI_WIN_STORE, StoreUiRepaint, "store");
    if (Idx >= 0) {
        return Idx;
    }

    Idx = AllocWindowSlot();
    if (Idx < 0) {
        return -1;
    }
    W = 920;
    H = 600;
    if (W + Margin * 2 > gGuiWindowManager.ScreenWidth) {
        W = gGuiWindowManager.ScreenWidth > Margin * 2 ? gGuiWindowManager.ScreenWidth - Margin * 2 : gGuiWindowManager.ScreenWidth / 2;
    }
    if (H + Margin * 2 > gGuiWindowManager.ScreenHeight) {
        H = gGuiWindowManager.ScreenHeight > Margin * 2 ? gGuiWindowManager.ScreenHeight - Margin * 2 : gGuiWindowManager.ScreenHeight / 2;
    }
    X = Margin;
    Y = Margin + 24;
    gGuiWindowManager.Windows[Idx].Active = 1;
    gGuiWindowManager.Windows[Idx].Kind = GUI_WIN_STORE;
    gGuiWindowManager.Windows[Idx].X = X;
    gGuiWindowManager.Windows[Idx].Y = Y;
    gGuiWindowManager.Windows[Idx].Width = W;
    gGuiWindowManager.Windows[Idx].Height = H;
    gGuiWindowManager.Windows[Idx].Background = ThemeSettingsClientBackground();
    gGuiWindowManager.Windows[Idx].Title = LocStr(MSG_APP_STORE);
    gGuiWindowManager.Windows[Idx].TermSet = 0;
    gGuiWindowManager.Windows[Idx].InputLen = 0;
    gGuiWindowManager.Windows[Idx].WaitPrompt = 0;
    gGuiWindowManager.Windows[Idx].PromptShown = 0;
    gGuiWindowManager.Windows[Idx].InputLine[0] = 0;

    OpenChromeDefer(Idx);
    StoreUiOpen();
    OpenFadeIn(Idx);
    DebugWrite("Gui: open store idx=");
    DebugHex32((UINT32)gGuiWindowManager.FocusWin);
    DebugWrite("\n");
    return gGuiWindowManager.FocusWin;
}


int GuiOpenFiles(void) {
    int Idx;
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;
    UINT32 Margin = 40;

    Idx = AllocWindowSlot();
    if (Idx < 0) {
        return -1;
    }
    W = 800;
    H = 560;
    if (W + Margin * 2 > gGuiWindowManager.ScreenWidth) {
        W = gGuiWindowManager.ScreenWidth > Margin * 2 ? gGuiWindowManager.ScreenWidth - Margin * 2 : gGuiWindowManager.ScreenWidth / 2;
    }
    if (H + Margin * 2 > gGuiWindowManager.ScreenHeight) {
        H = gGuiWindowManager.ScreenHeight > Margin * 2 ? gGuiWindowManager.ScreenHeight - Margin * 2 : gGuiWindowManager.ScreenHeight / 2;
    }
    X = Margin;
    Y = (gGuiWindowManager.ScreenHeight > H + Margin) ? (gGuiWindowManager.ScreenHeight - H - Margin) : Margin;
    gGuiWindowManager.Windows[Idx].Active = 1;
    gGuiWindowManager.Windows[Idx].Kind = GUI_WIN_FILES;
    gGuiWindowManager.Windows[Idx].X = X;
    gGuiWindowManager.Windows[Idx].Y = Y;
    gGuiWindowManager.Windows[Idx].Width = W;
    gGuiWindowManager.Windows[Idx].Height = H;
    gGuiWindowManager.Windows[Idx].Background = ThemeSettingsClientBackground();
    gGuiWindowManager.Windows[Idx].Title = LocStr(MSG_APP_FILES);
    gGuiWindowManager.Windows[Idx].TermSet = 0;
    gGuiWindowManager.Windows[Idx].InputLen = 0;
    gGuiWindowManager.Windows[Idx].WaitPrompt = 0;
    gGuiWindowManager.Windows[Idx].PromptShown = 0;
    gGuiWindowManager.Windows[Idx].InputLine[0] = 0;

    OpenChromeDefer(Idx);
    FilesUiOpen();
    OpenFadeIn(Idx);
    DebugWrite("Gui: open files idx=");
    DebugHex32((UINT32)gGuiWindowManager.FocusWin);
    DebugWrite("\n");
    return gGuiWindowManager.FocusWin;
}


int GuiOpenEdit(const char *Path) {
    int Idx;
    int Existing;
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;
    UINT32 Margin = 56;

    if (!Path || !Path[0]) {
        return -1;
    }

    /* Edit 亦单例：复用已有窗并换文件 */
    Existing = FocusExistingKind(GUI_WIN_EDIT, 0, "edit");
    if (Existing >= 0) {
        EditUiOpen(Path);
        DrawWindowAt(Existing);
        EditUiRepaint();
        BackupWindowAt(Existing);
        GuiFocusApply();
        BackupWindowAt(gGuiWindowManager.FocusWin);
        return gGuiWindowManager.FocusWin;
    }

    Idx = AllocWindowSlot();
    if (Idx < 0) {
        return -1;
    }
    W = 800;
    H = 520;
    if (W + Margin * 2 > gGuiWindowManager.ScreenWidth) {
        W = gGuiWindowManager.ScreenWidth > Margin * 2 ? gGuiWindowManager.ScreenWidth - Margin * 2 : gGuiWindowManager.ScreenWidth / 2;
    }
    if (H + Margin * 2 > gGuiWindowManager.ScreenHeight) {
        H = gGuiWindowManager.ScreenHeight > Margin * 2 ? gGuiWindowManager.ScreenHeight - Margin * 2 : gGuiWindowManager.ScreenHeight / 2;
    }
    X = Margin + 24;
    Y = Margin;
    gGuiWindowManager.Windows[Idx].Active = 1;
    gGuiWindowManager.Windows[Idx].Kind = GUI_WIN_EDIT;
    gGuiWindowManager.Windows[Idx].X = X;
    gGuiWindowManager.Windows[Idx].Y = Y;
    gGuiWindowManager.Windows[Idx].Width = W;
    gGuiWindowManager.Windows[Idx].Height = H;
    gGuiWindowManager.Windows[Idx].Background = ThemeSettingsClientBackground();
    gGuiWindowManager.Windows[Idx].Title = "Edit";
    gGuiWindowManager.Windows[Idx].TermSet = 0;
    gGuiWindowManager.Windows[Idx].InputLen = 0;
    gGuiWindowManager.Windows[Idx].WaitPrompt = 0;
    gGuiWindowManager.Windows[Idx].PromptShown = 0;
    gGuiWindowManager.Windows[Idx].InputLine[0] = 0;

    OpenChromeDefer(Idx);
    EditUiOpen(Path);
    EditUiRepaint();
    OpenFadeIn(Idx);
    DebugWrite("Gui: open edit idx=");
    DebugHex32((UINT32)gGuiWindowManager.FocusWin);
    DebugWrite("\n");
    return gGuiWindowManager.FocusWin;
}
