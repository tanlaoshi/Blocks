/*
 * Start.c — K36：底栏开始钮 + 三项菜单（Shell/Settings/Files）
 *
 * 【初学者】几何来自 Layout（可 LAYOUT.CFG）；顶栏标题仍由 WindowPaint。
 */
#include "Start.h"
#include "Font.h"
#include "Layout.h"
#include "Window.h"
#include "HalSerial.h"
#include "HalVideo.h"
#include "Locale.h"
#include "Theme.h"
#include "SerialConfig.h"

#define MENU_N 4u

static UINT32 gFbW;
static UINT32 gFbH;
static int gMenuOn;

static const int gItems[MENU_N] = {
    GUI_WIN_SHELL, GUI_WIN_SETTINGS, GUI_WIN_FILES, GUI_WIN_STORE
};

static LOC_MSG ItemLabel(int WinId) {
    if (WinId == GUI_WIN_SETTINGS) {
        return MSG_ICON_SETTINGS;
    }
    if (WinId == GUI_WIN_FILES) {
        return MSG_ICON_FILES;
    }
    if (WinId == GUI_WIN_STORE) {
        return MSG_ICON_STORE;
    }
    return MSG_ICON_SHELL;
}

void StartSetFb(UINT32 W, UINT32 H) {
    gFbW = W;
    gFbH = H;
    gMenuOn = 0;
}

void StartCloseMenu(void) {
    gMenuOn = 0;
}

int StartMenuOpen(void) {
    return gMenuOn;
}

void StartPaintBar(void) {
    UINT32 BarH;
    UINT32 By;
    UINT32 Bx;
    UINT32 BtnY;
    UINT32 Bw;
    UINT32 Bh;

    if (gFbW < 80u || gFbH < LayoutBarH()) {
        return;
    }
    BarH = LayoutBarH();
    By = LayoutContentBottom();
    HalVideoFillRect(0, By, gFbW, BarH, ThemeTaskbarBackground());
    LayoutStartBtn(&Bx, &BtnY, &Bw, &Bh);
    HalVideoFillRect(Bx, BtnY, Bw, Bh,
                     gMenuOn ? ThemeWindowTitleBar() : ThemeWindowBorder());
    FontDrawStringAt(Bx + LayoutPx(8u), BtnY + LayoutPx(3u),
                     LocStr(MSG_START), ThemeWindowTitleText());
}

void StartPaintMenu(void) {
    UINT32 Mx;
    UINT32 My;
    UINT32 Mw;
    UINT32 Mh;
    UINT32 i;
    UINT32 RowH;

    if (!gMenuOn || gFbW < 80u || gFbH < LayoutBarH()) {
        return;
    }
    LayoutStartMenu(&Mx, &My, &Mw, &Mh);
    RowH = LayoutPx(24u);
    HalVideoFillRect(Mx, My, Mw, Mh, ThemeWindowBorder());
    HalVideoFillRect(Mx + 1u, My + 1u, Mw - 2u, Mh - 2u, ThemeWindowClient());
    for (i = 0; i < MENU_N; i++) {
        FontDrawStringAt(Mx + LayoutPx(12u), My + LayoutPx(6u) + i * RowH,
                         LocStr(ItemLabel(gItems[i])), ThemeWindowTitleText());
    }
}

int StartHitButton(INT32 X, INT32 Y) {
    UINT32 Bx;
    UINT32 By;
    UINT32 Bw;
    UINT32 Bh;

    if (gFbH < LayoutBarH()) {
        return 0;
    }
    LayoutStartBtn(&Bx, &By, &Bw, &Bh);
    if (X < (INT32)Bx || Y < (INT32)By) {
        return 0;
    }
    if (X >= (INT32)(Bx + Bw) || Y >= (INT32)(By + Bh)) {
        return 0;
    }
    return 1;
}

int StartHitMenu(INT32 X, INT32 Y) {
    UINT32 Mx;
    UINT32 My;
    UINT32 Mw;
    UINT32 Mh;
    UINT32 Row;
    UINT32 RowH;
    UINT32 Pad;

    if (!gMenuOn || gFbH < LayoutBarH()) {
        return -1;
    }
    LayoutStartMenu(&Mx, &My, &Mw, &Mh);
    if (X < (INT32)Mx || Y < (INT32)My) {
        return -1;
    }
    if (X >= (INT32)(Mx + Mw) || Y >= (INT32)(My + Mh)) {
        return -1;
    }
    Pad = LayoutPx(4u);
    RowH = LayoutPx(24u);
    if (Y < (INT32)(My + Pad)) {
        return -2;
    }
    Row = (UINT32)(Y - (INT32)(My + Pad)) / RowH;
    if (Row >= MENU_N) {
        return -2;
    }
    return gItems[Row];
}

int StartToggle(void) {
    gMenuOn = !gMenuOn;
    HalSerialWriteChannel(SLOG_GUI, gMenuOn ? "Gui: start open\n"
                                                 : "Gui: start close\n");
    return 1;
}

int StartActivate(int WinId) {
    if (WinId != GUI_WIN_SHELL && WinId != GUI_WIN_SETTINGS &&
        WinId != GUI_WIN_FILES && WinId != GUI_WIN_STORE) {
        return 0;
    }
    gMenuOn = 0;
    if (WindowIsOn(WinId)) {
        WindowFocus(WinId);
    } else {
        WindowOpen(WinId);
    }
    HalSerialWriteChannel(SLOG_GUI, "Gui: start launch\n");
    return 1;
}
