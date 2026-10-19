/*
 * Menu.c — 开始菜单重建（系统项 + Game；Apps 见 MenuApps.c）
 */
#include "DesktopPrivate.h"


int MenuNameEqIgnoreCase(const char *A, const char *B) {
    char Ca;
    char Cb;

    if (!A || !B) {
        return 0;
    }
    while (*A && *B) {
        Ca = *A;
        Cb = *B;
        if (Ca >= 'A' && Ca <= 'Z') {
            Ca = (char)(Ca - 'A' + 'a');
        }
        if (Cb >= 'A' && Cb <= 'Z') {
            Cb = (char)(Cb - 'A' + 'a');
        }
        if (Ca != Cb) {
            return 0;
        }
        A++;
        B++;
    }
    return *A == 0 && *B == 0;
}

static int MenuMaxAppSlots(void) {
    UINT32 Sw;
    UINT32 Sh;
    UINT32 BarY;
    UINT32 Room;
    int MaxRows;

    TaskbarGeom(&BarY, &Sw, &Sh);
    (void)Sw;
    Room = BarY > 8u ? (BarY - 8u) : 0;
    MaxRows = (int)(Room / MENU_ITEM_H);
    if (MaxRows < 1) {
        MaxRows = 1;
    }
    if (MaxRows > MENU_APP_MAX) {
        MaxRows = MENU_APP_MAX;
    }
    return MaxRows;
}

static void MenuAddRow(DESKTOP_ACTION Act, const char *Label, const char *Path,
                       int Enabled, int IconSrc) {
    MENU_ROW *R;

    if (gMenuCount >= MENU_ROWS_MAX) {
        return;
    }
    R = &gMenuRows[gMenuCount++];
    R->Action = Act;
    R->Enabled = Enabled ? 1 : 0;
    R->IconSrc = IconSrc;
    StringCopy(R->Label, sizeof(R->Label), Label ? Label : "");
    StringCopy(R->Path, sizeof(R->Path), Path ? Path : "");
}

typedef struct {
    DESKTOP_ACTION Action;
    MSG_ID Msg;
    const char *Fallback;
    int IconSrc;
    int Enabled;
} MENU_SYS_ROW;

#define MENU_REG_MAX 8

static MENU_SYS_ROW gMenuReg[MENU_REG_MAX];
static int gMenuRegCount;

/* Apps、Game、重启、关机仍留在桌面。顺序看这张表。 */
static const MENU_SYS_ROW gMenuSysRows[] = {
    { DESKTOP_ACTION_APPS, MSG_ICON_APPS, "Apps", 3, 1 },
    { DESKTOP_ACTION_GAME, MSG_ICON_GAME, "Game", MENU_ICON_SRC_GAME, 1 },
    { DESKTOP_ACTION_REBOOT, MSG_ICON_REBOOT, "Reboot", MENU_ICON_SRC_REBOOT, 1 },
    { DESKTOP_ACTION_SHUTDOWN, MSG_ICON_SHUTDOWN, "Shutdown", MENU_ICON_SRC_POWER, 1 },
};

void DesktopMenuRegister(DESKTOP_ACTION Action, int Msg, const char *Fallback,
                         int IconSrc, int Enabled) {
    int i;

    if (!Fallback || gMenuRegCount >= MENU_REG_MAX) {
        return;
    }
    for (i = 0; i < gMenuRegCount; i++) {
        if (gMenuReg[i].Action == Action) {
            return;
        }
    }
    gMenuReg[gMenuRegCount].Action = Action;
    gMenuReg[gMenuRegCount].Msg = (MSG_ID)Msg;
    gMenuReg[gMenuRegCount].Fallback = Fallback;
    gMenuReg[gMenuRegCount].IconSrc = IconSrc;
    gMenuReg[gMenuRegCount].Enabled = Enabled ? 1 : 0;
    gMenuRegCount++;
}

/* 打开开始菜单时重建：系统项含 Apps/Game；Apps 二级见 FillStartMenuAppRows */
void RebuildStartMenu(void) {
    int i;
    const char *L;

    gMenuCount = 0;
    gMenuAppCount = 0;
    gMenuGameCount = 0;

    for (i = 0; i < gMenuRegCount; i++) {
        const MENU_SYS_ROW *Row = &gMenuReg[i];

        L = LocStr(Row->Msg);
        MenuAddRow(Row->Action, L ? L : Row->Fallback, 0, Row->Enabled, Row->IconSrc);
    }
    for (i = 0; i < (int)(sizeof(gMenuSysRows) / sizeof(gMenuSysRows[0])); i++) {
        const MENU_SYS_ROW *Row = &gMenuSysRows[i];

        L = LocStr(Row->Msg);
        MenuAddRow(Row->Action, L ? L : Row->Fallback, 0, Row->Enabled, Row->IconSrc);
        if (Row->Action == DESKTOP_ACTION_GAME) {
            /* Game 行用 GAME.BMP。二级紧跟在 Game 行之后，再列 Apps。 */
            FillStartMenuGameRows();
            FillStartMenuAppRows(MenuMaxAppSlots());
        }
    }
    /* 开机/Worker：先 TTF 栅进缓存，再测宽+绘制路径（裁到空区） */
    {
        HalVideoSetClipRegion(0, 0, 0, 0, 0);
        for (i = 0; i < gMenuCount; i++) {
            if (gMenuRows[i].Label[0]) {
                FontTtfPreheatUtf8(gMenuRows[i].Label);
                (void)FontStringWidth(gMenuRows[i].Label);
                HalVideoDrawStringAt(0, 0, gMenuRows[i].Label, 0);
            }
        }
        for (i = 0; i < gMenuAppCount; i++) {
            if (gMenuAppRows[i].Label[0]) {
                FontTtfPreheatUtf8(gMenuAppRows[i].Label);
                (void)FontStringWidth(gMenuAppRows[i].Label);
                HalVideoDrawStringAt(0, 0, gMenuAppRows[i].Label, 0);
            }
        }
        for (i = 0; i < gMenuGameCount; i++) {
            if (gMenuGameRows[i].Label[0]) {
                FontTtfPreheatUtf8(gMenuGameRows[i].Label);
                (void)FontStringWidth(gMenuGameRows[i].Label);
                HalVideoDrawStringAt(0, 0, gMenuGameRows[i].Label, 0);
            }
        }
        HalVideoClearClip();
    }
}
