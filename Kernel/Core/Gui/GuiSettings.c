/*
 * GuiSettings.c — K34：Settings 色板 → ThemeSet*（立即生效，不落盘）
 */
#include "GuiSettings.h"
#include "Font.h"
#include "Locale.h"
#include "Theme.h"
#include "HalVideo.h"

#define SW  28u
#define GAP 8u
#define ROW_H (SW + 20u)

static const UINT32 gDeskPal[4] = {
    0x001A1F24u, 0x00202840u, 0x00182820u, 0x00302028u
};
static const UINT32 gTitlePal[4] = {
    0x003D4F5Fu, 0x00405080u, 0x00306050u, 0x00604050u
};

static UINT32 gCx;
static UINT32 gCy;
static UINT32 gCw;
static UINT32 gCh;

void GuiSettingsPaintClient(UINT32 Cx, UINT32 Cy, UINT32 Cw, UINT32 Ch) {
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

int GuiSettingsClick(INT32 X, INT32 Y) {
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
            ThemeSetDesktopBackground(gDeskPal[i]);
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
            return 1;
        }
    }
    return 0;
}
