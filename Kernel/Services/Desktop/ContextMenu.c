/*
 * ContextMenu.c — 桌面右键菜单（设置 / 关机 / 重启）
 * 核心：Desktop.c
 */
#include "DesktopPrivate.h"

#define CTX_ROWS 3

static int gCtxOpen;
static UINT32 gCtxX;
static UINT32 gCtxY;
static UINT32 gCtxCoverX;
static UINT32 gCtxCoverY;
static UINT32 gCtxCoverW;
static UINT32 gCtxCoverH;

static const struct {
    DESKTOP_ACTION Action;
    MSG_ID Msg;
    const char *Fallback;
    int IconSrc;
} gCtxRows[CTX_ROWS] = {
    { DESKTOP_ACTION_SETTINGS, MSG_ICON_SETTINGS, "Settings", 1 },
    { DESKTOP_ACTION_SHUTDOWN, MSG_ICON_SHUTDOWN, "Shutdown", MENU_ICON_SRC_POWER },
    { DESKTOP_ACTION_REBOOT, MSG_ICON_REBOOT, "Reboot", MENU_ICON_SRC_REBOOT },
};

static void CtxGeom(UINT32 *X, UINT32 *Y, UINT32 *W, UINT32 *H) {
    *X = gCtxX;
    *Y = gCtxY;
    *W = MENU_W;
    *H = MENU_ITEM_H * CTX_ROWS;
}

static void CtxPlace(UINT32 X, UINT32 Y) {
    UINT32 Sw;
    UINT32 Sh;
    UINT32 BarY;
    UINT32 W = MENU_W;
    UINT32 H = MENU_ITEM_H * CTX_ROWS;

    TaskbarGeom(&BarY, &Sw, &Sh);
    if (X + W > Sw) {
        X = (Sw > W) ? (Sw - W) : 0;
    }
    if (Y + H > BarY && BarY > H) {
        Y = BarY - H;
    } else if (Y + H > Sh) {
        Y = (Sh > H) ? (Sh - H) : 0;
    }
    gCtxX = X;
    gCtxY = Y;
}

void DesktopContextMenuReset(void) {
    gCtxOpen = 0;
    gCtxCoverX = 0;
    gCtxCoverY = 0;
    gCtxCoverW = 0;
    gCtxCoverH = 0;
}

int DesktopContextMenuIsOpen(void) {
    return gCtxOpen ? 1 : 0;
}

void DesktopContextMenuClose(void) {
    if (!gCtxOpen) {
        return;
    }
    gCtxOpen = 0;
    RequestRefresh();
}

void DesktopContextMenuOpen(UINT32 X, UINT32 Y) {
    gMenuOpen = 0;
    gMenuAppsOpen = 0;
    gMenuGameOpen = 0;
    DesktopNetTrayClose();
    CtxPlace(X, Y);
    gCtxOpen = 1;
    RequestRefresh();
}

int DesktopContextMenuCoverGet(UINT32 *X, UINT32 *Y, UINT32 *W, UINT32 *H) {
    if (!X || !Y || !W || !H) {
        return 0;
    }
    if (gCtxCoverW == 0 || gCtxCoverH == 0) {
        *X = *Y = *W = *H = 0;
        return 0;
    }
    *X = gCtxCoverX;
    *Y = gCtxCoverY;
    *W = gCtxCoverW;
    *H = gCtxCoverH;
    return 1;
}

void DesktopContextMenuCoverClear(void) {
    gCtxCoverX = 0;
    gCtxCoverY = 0;
    gCtxCoverW = 0;
    gCtxCoverH = 0;
}

void DesktopDrawContextMenu(void) {
    UINT32 Mx;
    UINT32 My;
    UINT32 Mw;
    UINT32 Mh;
    int i;

    if (!gCtxOpen) {
        return;
    }
    CtxGeom(&Mx, &My, &Mw, &Mh);
    UiFillRectangle(Mx, My, Mw, Mh, ThemeControlFace());
    UiDrawRectangle(Mx, My, Mw, Mh, ThemeMenuBorder());
    for (i = 0; i < CTX_ROWS; i++) {
        MENU_ROW Row;
        UINT32 Iy = My + (UINT32)i * MENU_ITEM_H;
        UINT32 IconX = Mx + 6;
        UINT32 IconY = Iy + (MENU_ITEM_H > MENU_ICON_SZ ? (MENU_ITEM_H - MENU_ICON_SZ) / 2 : 0);
        UINT32 TextX = Mx + 10;
        const char *Lab;

        Row.Action = gCtxRows[i].Action;
        Row.Enabled = 1;
        Row.IconSrc = gCtxRows[i].IconSrc;
        Row.Label[0] = 0;
        Row.Path[0] = 0;
        UiDrawRectangle(Mx, Iy, Mw, MENU_ITEM_H, ThemeMenuSep());
        if (DrawMenuRowIcon(&Row, IconX, IconY)) {
            TextX = IconX + MENU_ICON_SZ + 6u;
        }
        Lab = LocStr(gCtxRows[i].Msg);
        if (!Lab || !Lab[0]) {
            Lab = gCtxRows[i].Fallback;
        }
        HalVideoDrawStringAt(TextX,
                             Iy + (MENU_ITEM_H > FontCellH()
                                       ? (MENU_ITEM_H - FontCellH()) / 2
                                       : 0),
                             Lab, ThemeMenuText());
    }
    gCtxCoverX = Mx;
    gCtxCoverY = My;
    gCtxCoverW = Mw;
    gCtxCoverH = Mh;
}

int DesktopContextMenuHandleClick(UINT32 X, UINT32 Y, DESKTOP_ACTION *OutAction) {
    UINT32 Mx;
    UINT32 My;
    UINT32 Mw;
    UINT32 Mh;
    int Row;

    if (OutAction) {
        *OutAction = DESKTOP_ACTION_NONE;
    }
    if (!gCtxOpen) {
        return 0;
    }
    CtxGeom(&Mx, &My, &Mw, &Mh);
    gCtxOpen = 0;
    RequestRefresh();
    if (X < Mx || Y < My || X >= Mx + Mw || Y >= My + Mh) {
        return 1;
    }
    Row = (int)((Y - My) / MENU_ITEM_H);
    if (OutAction && Row >= 0 && Row < CTX_ROWS) {
        *OutAction = gCtxRows[Row].Action;
    }
    return 1;
}

int DesktopContextMenuSample(UINT32 X, UINT32 Y, UINT32 *Out) {
    UINT32 Mx;
    UINT32 My;
    UINT32 Mw;
    UINT32 Mh;

    if (!gCtxOpen || !Out) {
        return 0;
    }
    CtxGeom(&Mx, &My, &Mw, &Mh);
    if (X < Mx || Y < My || X >= Mx + Mw || Y >= My + Mh) {
        return 0;
    }
    *Out = ThemeControlFace();
    return 1;
}

void DesktopContextMenuDrawIfHit(UINT32 X, UINT32 Y, UINT32 W, UINT32 H) {
    UINT32 Mx;
    UINT32 My;
    UINT32 Mw;
    UINT32 Mh;

    if (!gCtxOpen || W == 0 || H == 0) {
        return;
    }
    CtxGeom(&Mx, &My, &Mw, &Mh);
    if (RectsOverlap(X, Y, W, H, Mx, My, Mw, Mh)) {
        DesktopDrawContextMenu();
    }
}
