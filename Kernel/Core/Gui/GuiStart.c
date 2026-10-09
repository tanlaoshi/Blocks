/*
 * GuiStart.c — K36：底栏开始钮 + 三项菜单（Shell/Settings/Files）
 *
 * 【初学者】顶栏仍是 Blocks 标题；开始在底栏左侧；菜单向上弹出。
 */
#include "GuiStart.h"
#include "Font.h"
#include "GuiWin.h"
#include "HalSerial.h"
#include "HalVideo.h"
#include "Locale.h"
#include "Theme.h"
#include "ToySerialConfig.h"

#define GUI_BAR_H 28u
#define BTN_X     6u
/* "Start" = 5×16px；左垫 8 → 钮宽至少 88，留右缘 */
#define BTN_W     92u
#define BTN_H     20u
#define BTN_PAD_Y 4u
#define MENU_W    140u
#define MENU_ROW  24u
#define MENU_N    3u

static UINT32 gFbW;
static UINT32 gFbH;
static int gMenuOn;

static const int gItems[MENU_N] = {
    GUI_WIN_SHELL, GUI_WIN_SETTINGS, GUI_WIN_FILES
};

static LOC_MSG ItemLabel(int WinId) {
    if (WinId == GUI_WIN_SETTINGS) {
        return MSG_ICON_SETTINGS;
    }
    if (WinId == GUI_WIN_FILES) {
        return MSG_ICON_FILES;
    }
    return MSG_ICON_SHELL;
}

static UINT32 BarY(void) {
    return (gFbH > GUI_BAR_H) ? (gFbH - GUI_BAR_H) : 0;
}

static UINT32 BtnY(void) {
    return BarY() + BTN_PAD_Y;
}

void GuiStartSetFb(UINT32 W, UINT32 H) {
    gFbW = W;
    gFbH = H;
    gMenuOn = 0;
}

void GuiStartCloseMenu(void) {
    gMenuOn = 0;
}

int GuiStartMenuOpen(void) {
    return gMenuOn;
}

void GuiStartPaintBar(void) {
    UINT32 By;

    if (gFbW < 80u || gFbH < GUI_BAR_H) {
        return;
    }
    By = BarY();
    HalVideoFillRect(0, By, gFbW, GUI_BAR_H, ThemeTaskbarBackground());
    HalVideoFillRect(BTN_X, BtnY(), BTN_W, BTN_H,
                     gMenuOn ? ThemeWindowTitleBar() : ThemeWindowBorder());
    FontDrawStringAt(BTN_X + 8u, BtnY() + 3u, LocStr(MSG_START),
                     ThemeWindowTitleText());
}

void GuiStartPaintMenu(void) {
    UINT32 Mx;
    UINT32 My;
    UINT32 Mh;
    UINT32 i;

    if (!gMenuOn || gFbW < 80u || gFbH < GUI_BAR_H) {
        return;
    }
    Mh = MENU_N * MENU_ROW + 8u;
    Mx = BTN_X;
    My = (BarY() > Mh) ? (BarY() - Mh) : 0;
    HalVideoFillRect(Mx, My, MENU_W, Mh, ThemeWindowBorder());
    HalVideoFillRect(Mx + 1u, My + 1u, MENU_W - 2u, Mh - 2u, ThemeWindowClient());
    for (i = 0; i < MENU_N; i++) {
        FontDrawStringAt(Mx + 12u, My + 6u + i * MENU_ROW,
                         LocStr(ItemLabel(gItems[i])), ThemeWindowTitleText());
    }
}

int GuiStartHitButton(INT32 X, INT32 Y) {
    UINT32 Bx = BTN_X;
    UINT32 By = BtnY();

    if (gFbH < GUI_BAR_H) {
        return 0;
    }
    if (X < (INT32)Bx || Y < (INT32)By) {
        return 0;
    }
    if (X >= (INT32)(Bx + BTN_W) || Y >= (INT32)(By + BTN_H)) {
        return 0;
    }
    return 1;
}

int GuiStartHitMenu(INT32 X, INT32 Y) {
    UINT32 Mx;
    UINT32 My;
    UINT32 Mh;
    UINT32 Row;

    if (!gMenuOn || gFbH < GUI_BAR_H) {
        return -1;
    }
    Mh = MENU_N * MENU_ROW + 8u;
    Mx = BTN_X;
    My = (BarY() > Mh) ? (BarY() - Mh) : 0;
    if (X < (INT32)Mx || Y < (INT32)My) {
        return -1;
    }
    if (X >= (INT32)(Mx + MENU_W) || Y >= (INT32)(My + Mh)) {
        return -1;
    }
    if (Y < (INT32)(My + 4u)) {
        return -2;
    }
    Row = (UINT32)(Y - (INT32)(My + 4u)) / MENU_ROW;
    if (Row >= MENU_N) {
        return -2;
    }
    return gItems[Row];
}

int GuiStartToggle(void) {
    gMenuOn = !gMenuOn;
    HalSerialWriteChannel(TOY_SLOG_GUI, gMenuOn ? "Gui: start open\n"
                                                 : "Gui: start close\n");
    return 1;
}

int GuiStartActivate(int WinId) {
    if (WinId != GUI_WIN_SHELL && WinId != GUI_WIN_SETTINGS &&
        WinId != GUI_WIN_FILES) {
        return 0;
    }
    gMenuOn = 0;
    if (GuiWinIsOn(WinId)) {
        GuiWinFocus(WinId);
    } else {
        GuiWinOpen(WinId);
    }
    HalSerialWriteChannel(TOY_SLOG_GUI, "Gui: start launch\n");
    return 1;
}
