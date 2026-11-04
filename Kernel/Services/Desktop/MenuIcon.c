/*
 * MenuIcon.c — 开始菜单行图标（关机/重启专用 BMP；系统项用 menu/ 原版）
 */
#include "DesktopPrivate.h"

static const char *const MenuSysPaths[DESKTOP_SYS_ICON_COUNT] = {
    "Assets/Icons/menu/SHELL.BMP", "Assets/Icons/menu/SET.BMP",
    "Assets/Icons/menu/FILES.BMP", "Assets/Icons/menu/STORE.BMP",
    "Assets/Icons/menu/INFO.BMP"
};

void DesktopLoadMenuSysIcons(void) {
    int i;
    static const char *const Tags[DESKTOP_SYS_ICON_COUNT] = {
        "menu: shell", "menu: set", "menu: files", "menu: store", "menu: info"
    };

    for (i = 0; i < DESKTOP_SYS_ICON_COUNT; i++) {
        gMenuSysBmpReady[i] = 0;
        BmpFree(&gMenuSysBmp[i]);
        gMenuSysBmpReady[i] =
            LoadBmpPath(MenuSysPaths[i], &gMenuSysBmp[i], ICON_FILE_MAX, Tags[i]);
    }
}

/* 系统槽优先 menu/；其它仍用桌面 Bmp */
const BMP_IMAGE *MenuIconFace(int IconSrc) {
    if (IconSrc >= 0 && IconSrc < DESKTOP_SYS_ICON_COUNT &&
        gMenuSysBmpReady[IconSrc]) {
        return &gMenuSysBmp[IconSrc];
    }
    if (IconSrc >= 0 && IconSrc < DESKTOP_ICON_COUNT &&
        gIcons[IconSrc].BmpReady) {
        return &gIcons[IconSrc].Bmp;
    }
    return 0;
}

/* 返回 1 已画图标（调用方右移文字） */
int DrawMenuRowIcon(const MENU_ROW *R, UINT32 IconX, UINT32 IconY) {
    const BMP_IMAGE *Face;

    if (!R) {
        return 0;
    }
    /* Action 优先：IconSrc 6/7 曾与 Apps 槽冲突 */
    if (R->Action == DESKTOP_ACTION_SHUTDOWN) {
        if (gPowerBmpReady) {
            BlitBmpScaledRaw(IconX, IconY, MENU_ICON_SZ, MENU_ICON_SZ, &gPowerBmp);
        } else {
            GraphicsFillRectangle(IconX, IconY, MENU_ICON_SZ, MENU_ICON_SZ,
                            ThemeIconFallbackPower());
        }
        return 1;
    }
    if (R->Action == DESKTOP_ACTION_GAME || R->IconSrc == MENU_ICON_SRC_GAME) {
        if (gGameBmpReady) {
            BlitBmpScaledRaw(IconX, IconY, MENU_ICON_SZ, MENU_ICON_SZ, &gGameBmp);
        } else {
            GraphicsFillRectangle(IconX, IconY, MENU_ICON_SZ, MENU_ICON_SZ, 0x00907020u);
        }
        return 1;
    }
    if (R->Action == DESKTOP_ACTION_REBOOT) {
        if (gRebootBmpReady) {
            BlitBmpScaledRaw(IconX, IconY, MENU_ICON_SZ, MENU_ICON_SZ, &gRebootBmp);
        } else {
            GraphicsFillRectangle(IconX, IconY, MENU_ICON_SZ, MENU_ICON_SZ,
                            ThemeIconFallbackReboot());
        }
        return 1;
    }
    if (R->IconSrc >= 0 && R->IconSrc < DESKTOP_ICON_COUNT) {
        Face = MenuIconFace(R->IconSrc);
        if (Face) {
            BlitBmpScaledRaw(IconX, IconY, MENU_ICON_SZ, MENU_ICON_SZ, Face);
        } else {
            GraphicsFillRectangle(IconX, IconY, MENU_ICON_SZ, MENU_ICON_SZ,
                            gIcons[R->IconSrc].IconColor);
        }
        return 1;
    }
    if (R->Action == DESKTOP_ACTION_EXEC) {
        /* IconSrc<0：无桌面槽时勿回落 Shell，用 Store 作通用应用标 */
        Face = MenuIconFace(3);
        if (Face) {
            BlitBmpScaledRaw(IconX, IconY, MENU_ICON_SZ, MENU_ICON_SZ, Face);
        } else {
            GraphicsFillRectangle(IconX, IconY, MENU_ICON_SZ, MENU_ICON_SZ,
                            gIcons[3].IconColor);
        }
        return 1;
    }
    return 0;
}
