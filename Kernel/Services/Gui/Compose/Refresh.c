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

    UINT32 Cx;
    UINT32 Cy;
    UINT32 Cw;
    UINT32 Ch;
    int HadCtx;
    int CtxOpen;

    Open = DesktopStartMenuIsOpen();
    CtxOpen = DesktopContextMenuIsOpen();
    HadCover = DesktopMenuCoverGet(&Ox, &Oy, &Ow, &Oh);
    HadCtx = DesktopContextMenuCoverGet(&Cx, &Cy, &Cw, &Ch);

    if (Open) {
        /* 弹出开始菜单：其它窗失焦；禁止半透/chrome 镂进菜单 */
        if (gGuiWindowManager.FocusWin >= 0) {
            GuiFocusSave();
            gGuiWindowManager.FocusWin = -1;
        }
        gGuiWindowManager.HoverWin = -1;
    }

    if (Open || CtxOpen || HadCover || HadCtx) {
        ComposeBeginEraseCursor();
        HalVideoClearClip();
        if (HadCover && Ow > 0 && Oh > 0) {
            GuiClearIconDragFootprint(Ox, Oy, Ow, Oh);
        }
        if (HadCtx && Cw > 0 && Ch > 0) {
            GuiClearIconDragFootprint(Cx, Cy, Cw, Ch);
        }
        if (Open) {
            DesktopDrawStartMenu();
        } else {
            DesktopMenuCoverClear();
        }
        if (CtxOpen) {
            DesktopDrawContextMenu();
        } else {
            DesktopContextMenuCoverClear();
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
