/*
 * Settings.c — K34/K37：预设色板（墨/石/松）+ 标题石色
 *
 * 【初学者】点桌面色块 = 整套 ThemeApplyNamed；点第二行只改标题条。
 */
#include "Settings.h"
#include "Font.h"
#include "Locale.h"
#include "Theme.h"
#include "HalVideo.h"

#define SW  28u
#define GAP 8u
#define ROW_H (SW + 20u)

/* 桌面行：整套预设预览色（ink / slate / pine / 暖褐） */
static const UINT32 gDeskPal[4] = {
    0x001C1B1Au, 0x0022262Bu, 0x001A201Cu, 0x00241C18u
};
static const char *gDeskName[4] = {"ink", "slate", "pine", 0};
/* 标题行：暖石 / 烟灰 / 松皮 / 陶土 —— 无青蓝 */
static const UINT32 gTitlePal[4] = {
    0x004A4540u, 0x00485058u, 0x003E4A40u, 0x00584840u
};

static UINT32 gCx;
static UINT32 gCy;
static UINT32 gCw;
static UINT32 gCh;

void SettingsPaintClient(UINT32 Cx, UINT32 Cy, UINT32 Cw, UINT32 Ch) {
    UINT32 i;
    UINT32 X0;
    UINT32 Y0;

    gCx = Cx;
    gCy = Cy;
    gCw = Cw;
    gCh = Ch;
    if (Cw < 40u || Ch < 80u) {
        return;
    }
    FontDrawStringAt(Cx + 12u, Cy + 10u, LocStr(MSG_SETTINGS_HINT),
                     ThemeWindowTitleText());
    X0 = Cx + 12u;
    Y0 = Cy + 36u;
    for (i = 0; i < 4u; i++) {
        HalVideoFillRect(X0 + i * (SW + GAP), Y0, SW, SW, gDeskPal[i]);
    }
    Y0 += ROW_H;
    for (i = 0; i < 4u; i++) {
        HalVideoFillRect(X0 + i * (SW + GAP), Y0, SW, SW, gTitlePal[i]);
    }
}

int SettingsClick(INT32 X, INT32 Y) {
    UINT32 i;
    UINT32 X0;
    UINT32 Y0;
    UINT32 Sx;
    UINT32 Sy;

    if (gCw < 40u || gCh < 80u) {
        return 0;
    }
    X0 = gCx + 12u;
    Y0 = gCy + 36u;
    for (i = 0; i < 4u; i++) {
        Sx = X0 + i * (SW + GAP);
        Sy = Y0;
        if (X >= (INT32)Sx && Y >= (INT32)Sy &&
            X < (INT32)(Sx + SW) && Y < (INT32)(Sy + SW)) {
            if (gDeskName[i] != 0) {
                (void)ThemeApplyNamed(gDeskName[i]);
            } else {
                ThemeSetDesktopBackground(gDeskPal[i]);
                ThemeSetTaskbarBackground(0x00342824u);
                ThemeSetWindowTitleBar(0x00584840u);
            }
            (void)ThemeSaveConfiguration();
            return 1;
        }
    }
    Y0 += ROW_H;
    for (i = 0; i < 4u; i++) {
        Sx = X0 + i * (SW + GAP);
        Sy = Y0;
        if (X >= (INT32)Sx && Y >= (INT32)Sy &&
            X < (INT32)(Sx + SW) && Y < (INT32)(Sy + SW)) {
            ThemeSetWindowTitleBar(gTitlePal[i]);
            (void)ThemeSaveConfiguration();
            return 1;
        }
    }
    return 0;
}
