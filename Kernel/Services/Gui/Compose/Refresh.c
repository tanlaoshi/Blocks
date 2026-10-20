/*
 * Refresh.c — GuiRefreshDesktop（gui-g0-map + gui-g4-mark）
 *
 * Desktop RequestRefresh 钩子（WindowManager 挂上）。
 *   开始菜单 / 右键 / 网托开着 / 覆盖区刚关 → 只画菜单层 + PresentFlush（快路径）
 *   否则 → GuiComposeDamage（有 Mark 则脏矩形，无则整屏 ThemeScene）
 * 谁调 RequestRefresh / Mark：见 Damage.c 头；栏/托盘 Mark 见 Desktop/Mark.c。
 */
#include "GuiPrivate.h"
#include "HalVideo.h"
#include "Desktop.h"

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
    int TrayOpen;

    Open = DesktopStartMenuIsOpen();
    CtxOpen = DesktopContextMenuIsOpen();
    TrayOpen = DesktopNetTrayIsOpen();
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

    if (Open || CtxOpen || TrayOpen || HadCover || HadCtx) {
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
        GraphicsIrqEnter();
        CursorPaint();
        GraphicsIrqLeave();
        HalVideoPresentFlush();
        ComposeEnd();
        return;
    }

    GuiComposeDamage();
}
