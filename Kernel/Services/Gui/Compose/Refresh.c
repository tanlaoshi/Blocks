/*
 * GuiComposeRefresh.c — GuiRefreshDesktop（开始菜单局部刷新）
 */
#include "GuiPrivate.h"
#include "HalVideo.h"
#include "Desktop.h"

/* 开/关/flyout 只动菜单覆盖区；其它桌面刷新仍走全量 Compose */
void GuiRefreshDesktop(void) {
    UINT32 Ox;
    UINT32 Oy;
    UINT32 Ow;
    UINT32 Oh;
    int HadCover;
    int Open;

    Open = DesktopStartMenuIsOpen();
    HadCover = DesktopMenuCoverGet(&Ox, &Oy, &Ow, &Oh);

    if (Open) {
        /* 弹出开始菜单：其它窗失焦；禁止半透/chrome 镂进菜单 */
        if (gGuiWindowManager.FocusWin >= 0) {
            GuiFocusSave();
            gGuiWindowManager.FocusWin = -1;
        }
        gGuiWindowManager.HoverWin = -1;
    }

    if (Open || HadCover) {
        ComposeBeginEraseCursor();
        HalVideoClearClip();
        if (HadCover && Ow > 0 && Oh > 0) {
            GuiClearIconDragFootprint(Ox, Oy, Ow, Oh);
        }
        if (Open) {
            DesktopDrawStartMenu();
        } else {
            DesktopMenuCoverClear();
        }
        DesktopDrawNetTrayPopup();
        GfxIrqEnter();
        CursorPaint();
        GfxIrqLeave();
        HalVideoPresentFlush();
        ComposeEnd();
        return;
    }

    GuiComposeDamage();
}
