/*
 * Mark.c — 桌面局部脏矩形（gui-g4-mark）
 *
 * RequestRefresh → GuiRefreshDesktop：菜单/右键/覆盖快路径不看 Mark；
 * 否则 GuiComposeDamage，无 Mark → 整屏 ThemeScene。
 * 本文件在栏/菜单/网托开关前并 AABB，避免托盘等掉进 4K。
 */
#include "DesktopPrivate.h"
#include "Gui.h"

void DesktopMarkRect(UINT32 X, UINT32 Y, UINT32 W, UINT32 H) {
    if (W > 0 && H > 0) {
        GuiMarkDamage(X, Y, W, H);
    }
}

void DesktopMarkTaskbar(void) {
    UINT32 BarY;
    UINT32 Sw;
    UINT32 Sh;

    TaskbarGeom(&BarY, &Sw, &Sh);
    DesktopMarkRect(0, BarY, Sw, TASKBAR_H);
}

void DesktopMarkMenuArea(void) {
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;

    if (gMenuOpen) {
        DesktopMenuCoverUpdate();
    }
    if (DesktopMenuCoverGet(&X, &Y, &W, &H) && W > 0 && H > 0) {
        DesktopMarkRect(X, Y, W, H);
    }
}

void DesktopMarkContextMenuArea(void) {
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;

    /* 关菜单后仍可用上次 Draw 写入的 cover 擦洞 */
    if (DesktopContextMenuCoverGet(&X, &Y, &W, &H) && W > 0 && H > 0) {
        DesktopMarkRect(X, Y, W, H);
    }
}
