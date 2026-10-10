/*
 * Pointer.c — 鼠轮询、点击分发、拖窗
 *
 * 对标现网 CodeD-Services/GuiPointer/（实现略薄；目录即命名空间不叠 Gui）。
 */
#include "Gui.h"
#include "GuiPrivate.h"
#include "Desktop.h"
#include "Files.h"
#include "Layout.h"
#include "Settings.h"
#include "Start.h"
#include "StoreUi.h"
#include "StoreJob.h"
#include "Window.h"
#include "Console.h"
#include "HalPs2.h"
#include "HalPs2Mouse.h"
#include "HalUsbHid.h"

static int gDragging;
static int gDragWin;
static INT32 gDragOffX;
static INT32 gDragOffY;
static UINT8 gPrevButtons;
static UINT32 gDragIdle;

void PointerReset(void) {
    gDragging = 0;
    gDragWin = -1;
    gDragOffX = 0;
    gDragOffY = 0;
    gPrevButtons = 0;
    gDragIdle = 0;
}

int GuiPoll(void) {
    HAL_MOUSE_PACKET Pkt;
    INT32 AccX = 0;
    INT32 AccY = 0;
    UINT8 LastBtn = gPrevButtons;
    int Got = 0;
    int SawPress = 0;
    int SawRelease = 0;
    int WasDragging;
    int Hit;
    INT32 CurX = 0;
    INT32 CurY = 0;

    /* K48：商店窗入队的装包在此推进（Shell 路径自带 Step） */
    if (StoreJobStep()) {
        if (WindowIsOn(GUI_WIN_STORE)) {
            StoreUiRefresh();
            WindowPaintDesktop();
            WindowCompose();
            WindowPresentFull();
        }
    }
    if (!DesktopReady() || !CursorIsEnabled()) {
        return 0;
    }
    DesktopPollTick();
    HalUsbHidService();
    HalPs2Poll();
    while (HalUsbHidPollMouse(&Pkt) ||
           (!HalUsbHidMouseReady() && HalPs2MousePoll(&Pkt))) {
        Got = 1;
        if (Pkt.Absolute) {
            CursorSetFromAbsolute(Pkt.Dx, Pkt.Dy);
        } else {
            AccX += Pkt.Dx;
            AccY += Pkt.Dy;
        }
        if ((Pkt.Buttons & 0x1u) != 0 && (LastBtn & 0x1u) == 0) {
            SawPress = 1;
        }
        if ((Pkt.Buttons & 0x1u) == 0 && (LastBtn & 0x1u) != 0) {
            SawRelease = 1;
        }
        LastBtn = Pkt.Buttons;
    }
    if (!Got) {
        if (gDragging) {
            gDragIdle++;
            if (gDragIdle >= 20000u) {
                gDragging = 0;
                gDragWin = -1;
                gPrevButtons = 0;
                gDragIdle = 0;
                ConsoleRefreshBanner();
            }
        }
        return 0;
    }
    gDragIdle = 0;
    GuiCursorHide();

    if (AccX != 0 || AccY != 0) {
        CursorMoveBy(AccX, AccY);
    }
    CursorGetPosition(&CurX, &CurY);

    if (SawPress) {
        int StartHandled = 0;

        if (StartHitButton(CurX, CurY)) {
            (void)StartToggle();
            StartHandled = 1;
        } else if (StartMenuOpen()) {
            int MenuHit = StartHitMenu(CurX, CurY);
            if (MenuHit >= 0) {
                (void)StartActivate(MenuHit);
                StartHandled = 1;
            } else if (MenuHit == -2) {
                StartHandled = 1; /* 菜单内空白 */
            } else {
                StartCloseMenu();
                StartHandled = 1; /* 先收起；再点一次点下方 */
            }
        }

        if (StartHandled) {
            gDragging = 0;
            gDragWin = -1;
            WindowPaintDesktop();
            WindowCompose();
            WindowPresentFull();
        } else {
            Hit = WindowHit(CurX, CurY);
            if (Hit >= 0 && WindowInClose(Hit, CurX, CurY)) {
                gDragging = 0;
                gDragWin = -1;
                WindowClose(Hit);
            } else if (Hit >= 0 && WindowInTitle(Hit, CurX, CurY)) {
                INT32 WindowX = 0;
                INT32 WindowY = 0;
                WindowFocus(Hit);
                (void)WindowGetPos(Hit, &WindowX, &WindowY);
                gDragging = 1;
                gDragWin = Hit;
                gDragOffX = CurX - WindowX;
                gDragOffY = CurY - WindowY;
            } else if (Hit == GUI_WIN_SETTINGS) {
                WindowFocus(Hit);
                gDragging = 0;
                gDragWin = -1;
                if (SettingsClick(CurX, CurY)) {
                    WindowPaintDesktop();
                    WindowCompose();
                    WindowPresentFull();
                }
            } else if (Hit == GUI_WIN_FILES) {
                WindowFocus(Hit);
                gDragging = 0;
                gDragWin = -1;
                /* 与 Settings 相同：点选/New/Del 改状态后必须重画，否则像「没反应」 */
                if (FilesClick(CurX, CurY)) {
                    WindowPaintDesktop();
                    WindowCompose();
                    WindowPresentFull();
                }
            } else if (Hit == GUI_WIN_STORE) {
                WindowFocus(Hit);
                gDragging = 0;
                gDragWin = -1;
                if (StoreUiClick(CurX, CurY)) {
                    WindowPaintDesktop();
                    WindowCompose();
                    WindowPresentFull();
                }
            } else if (Hit >= 0) {
                WindowFocus(Hit);
                gDragging = 0;
                gDragWin = -1;
            } else {
                int Icon = DesktopHitIcon(CurX, CurY);
                if (Icon >= 0) {
                    if (DesktopClickIcon(Icon, CurX, CurY)) {
                        WindowPaintDesktop();
                        WindowCompose();
                        WindowPresentFull();
                    }
                    gDragging = 0;
                    gDragWin = -1;
                } else if ((UINT32)CurY >= LayoutContentTop() &&
                           (UINT32)CurY < LayoutContentBottom()) {
                    WindowUnfocusAll();
                    gDragging = 0;
                    gDragWin = -1;
                }
            }
        }
    }

    WasDragging = gDragging;
    if (gDragging && gDragWin >= 0 && (LastBtn & 0x1u) != 0 &&
        (AccX != 0 || AccY != 0)) {
        WindowMoveTo(gDragWin, CurX - gDragOffX, CurY - gDragOffY, 0);
    }

    if (SawRelease) {
        if (WasDragging && gDragWin == GUI_WIN_SHELL) {
            ConsoleRefreshBanner();
        }
        gDragging = 0;
        gDragWin = -1;
    }

    GuiCursorShow();
    gPrevButtons = LastBtn;
    return 1;
}
