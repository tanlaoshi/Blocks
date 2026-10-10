/*
 * Start.c — 底栏开始钮 + 弹出菜单（Shell/Settings/Files/Store）
 *
 * 【初学者】
 * - 分层：Core/Gui；几何来自 Layout
 * - 对外：StartSetFb / StartPaintBar / StartPaintMenu / StartHit* /
 *         StartToggle / StartActivate / StartCloseMenu / StartMenuOpen
 * - 顶栏标题仍由 WindowPaint
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

/*
 * ItemLabel — 菜单项对应本地化标签
 *
 * 谁调用：仅 StartPaintMenu。
 */
static LOC_MSG ItemLabel(int WindowId) {
    if (WindowId == GUI_WIN_SETTINGS) {
        return MSG_ICON_SETTINGS;
    }
    if (WindowId == GUI_WIN_FILES) {
        return MSG_ICON_FILES;
    }
    if (WindowId == GUI_WIN_STORE) {
        return MSG_ICON_STORE;
    }
    return MSG_ICON_SHELL;
}

/*
 * StartSetFb — 记录分辨率并收起菜单
 *
 * 谁调用：GuiInitialize。
 */
void StartSetFb(UINT32 W, UINT32 H) {
    gFbW = W;
    gFbH = H;
    gMenuOn = 0;
}

/*
 * StartCloseMenu — 关闭开始菜单
 *
 * 谁调用：GuiPoll（点菜单外）；StartActivate。
 */
void StartCloseMenu(void) {
    gMenuOn = 0;
}

/*
 * StartMenuOpen — 菜单是否展开
 *
 * 谁调用：GuiPoll 分支；StartPaintMenu/HitMenu。
 */
int StartMenuOpen(void) {
    return gMenuOn;
}

/*
 * StartPaintBar — 画底栏与开始钮
 *
 * 谁调用：Window 桌面合成路径。
 */
void StartPaintBar(void) {
    UINT32 BarH;
    UINT32 By;
    UINT32 Bx;
    UINT32 ButtonY;
    UINT32 Bw;
    UINT32 Bh;

    if (gFbW < 80u || gFbH < LayoutBarH()) {
        return;
    }
    BarH = LayoutBarH();
    By = LayoutContentBottom();
    HalVideoFillRect(0, By, gFbW, BarH, ThemeTaskbarBackground());
    LayoutStartButton(&Bx, &ButtonY, &Bw, &Bh);
    HalVideoFillRect(Bx, ButtonY, Bw, Bh,
                     gMenuOn ? ThemeWindowTitleBar() : ThemeWindowBorder());
    FontDrawStringAt(Bx + LayoutPx(8u), ButtonY + LayoutPx(3u),
                     LocStr(MSG_START), ThemeWindowTitleText());
}

/*
 * StartPaintMenu — 画弹出菜单四项
 *
 * 谁调用：菜单打开时的桌面绘制。
 */
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

/*
 * StartHitButton — 是否点中开始钮
 *
 * 谁调用：GuiPoll。
 * 返回：1 命中；0 否
 */
int StartHitButton(INT32 X, INT32 Y) {
    UINT32 Bx;
    UINT32 By;
    UINT32 Bw;
    UINT32 Bh;

    if (gFbH < LayoutBarH()) {
        return 0;
    }
    LayoutStartButton(&Bx, &By, &Bw, &Bh);
    if (X < (INT32)Bx || Y < (INT32)By) {
        return 0;
    }
    if (X >= (INT32)(Bx + Bw) || Y >= (INT32)(By + Bh)) {
        return 0;
    }
    return 1;
}

/*
 * StartHitMenu — 菜单内命中哪一项
 *
 * 谁调用：GuiPoll（菜单已开）。
 * 返回：GUI_WIN_*；-1 在菜单外；-2 菜单内空白
 */
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

/*
 * StartToggle — 开/关开始菜单
 *
 * 谁调用：GuiPoll（点中开始钮）。
 * 返回：1（总是消费点击）
 */
int StartToggle(void) {
    gMenuOn = !gMenuOn;
    HalSerialWriteChannel(SLOG_GUI, gMenuOn ? "Gui: start open\n"
                                                 : "Gui: start close\n");
    return 1;
}

/*
 * StartActivate — 从菜单启动/聚焦窗并关菜单
 *
 * 谁调用：GuiPoll（StartHitMenu >= 0）。
 * 返回：1 已处理；0 WindowId 非法
 */
int StartActivate(int WindowId) {
    if (WindowId != GUI_WIN_SHELL && WindowId != GUI_WIN_SETTINGS &&
        WindowId != GUI_WIN_FILES && WindowId != GUI_WIN_STORE) {
        return 0;
    }
    gMenuOn = 0;
    if (WindowIsOn(WindowId)) {
        WindowFocus(WindowId);
    } else {
        WindowOpen(WindowId);
    }
    HalSerialWriteChannel(SLOG_GUI, "Gui: start launch\n");
    return 1;
}
