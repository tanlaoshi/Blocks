/*
 * Settings.c — 设置窗：桌面预设色板 + 标题条色
 *
 * 【初学者】
 * - 分层：Core/Gui
 * - 对外：SettingsPaintClient / SettingsClick
 * - 点桌面色块 = ThemeApplyNamed；点第二行只改标题条
 */
#include "Settings.h"
#include "Font.h"
#include "Locale.h"
#include "Theme.h"
#include "HalVideo.h"

#define SWATCH_SIZE  28u
#define SWATCH_GAP   8u
#define ROW_HEIGHT   (SWATCH_SIZE + 20u)

static const UINT32 gDesktopPalette[4] = {
    0x001C1B1Au, 0x0022262Bu, 0x001A201Cu, 0x00241C18u
};
static const char *gDesktopPresetName[4] = {"ink", "slate", "pine", 0};
static const UINT32 gTitlePalette[4] = {
    0x004A4540u, 0x00485058u, 0x003E4A40u, 0x00584840u
};

static UINT32 gClientX;
static UINT32 gClientY;
static UINT32 gClientWidth;
static UINT32 gClientHeight;

/*
 * SettingsPaintClient — 画提示与两行色块
 *
 * 谁调用：Window 客户区绘制（Settings 窗）。
 */
void SettingsPaintClient(UINT32 ClientX, UINT32 ClientY, UINT32 ClientWidth,
                         UINT32 ClientHeight) {
    UINT32 i;
    UINT32 X0;
    UINT32 Y0;

    gClientX = ClientX;
    gClientY = ClientY;
    gClientWidth = ClientWidth;
    gClientHeight = ClientHeight;
    if (ClientWidth < 40u || ClientHeight < 80u) {
        return;
    }
    FontDrawStringAt(ClientX + 12u, ClientY + 10u, LocStr(MSG_SETTINGS_HINT),
                     ThemeWindowTitleText());
    X0 = ClientX + 12u;
    Y0 = ClientY + 36u;
    for (i = 0; i < 4u; i++) {
        HalVideoFillRect(X0 + i * (SWATCH_SIZE + SWATCH_GAP), Y0, SWATCH_SIZE,
                         SWATCH_SIZE, gDesktopPalette[i]);
    }
    Y0 += ROW_HEIGHT;
    for (i = 0; i < 4u; i++) {
        HalVideoFillRect(X0 + i * (SWATCH_SIZE + SWATCH_GAP), Y0, SWATCH_SIZE,
                         SWATCH_SIZE, gTitlePalette[i]);
    }
}

/*
 * SettingsClick — 点色块改主题并落盘
 *
 * 谁调用：GuiPoll（GUI_WIN_SETTINGS）。
 * 返回：1 已改主题需重画；0 未点中
 */
int SettingsClick(INT32 X, INT32 Y) {
    UINT32 i;
    UINT32 X0;
    UINT32 Y0;
    UINT32 SwatchX;
    UINT32 SwatchY;

    if (gClientWidth < 40u || gClientHeight < 80u) {
        return 0;
    }
    X0 = gClientX + 12u;
    Y0 = gClientY + 36u;
    for (i = 0; i < 4u; i++) {
        SwatchX = X0 + i * (SWATCH_SIZE + SWATCH_GAP);
        SwatchY = Y0;
        if (X >= (INT32)SwatchX && Y >= (INT32)SwatchY &&
            X < (INT32)(SwatchX + SWATCH_SIZE) &&
            Y < (INT32)(SwatchY + SWATCH_SIZE)) {
            if (gDesktopPresetName[i] != 0) {
                (void)ThemeApplyNamed(gDesktopPresetName[i]);
            } else {
                ThemeSetDesktopBackground(gDesktopPalette[i]);
                ThemeSetTaskbarBackground(0x00342824u);
                ThemeSetWindowTitleBar(0x00584840u);
            }
            (void)ThemeSaveConfiguration();
            return 1;
        }
    }
    Y0 += ROW_HEIGHT;
    for (i = 0; i < 4u; i++) {
        SwatchX = X0 + i * (SWATCH_SIZE + SWATCH_GAP);
        SwatchY = Y0;
        if (X >= (INT32)SwatchX && Y >= (INT32)SwatchY &&
            X < (INT32)(SwatchX + SWATCH_SIZE) &&
            Y < (INT32)(SwatchY + SWATCH_SIZE)) {
            ThemeSetWindowTitleBar(gTitlePalette[i]);
            (void)ThemeSaveConfiguration();
            return 1;
        }
    }
    return 0;
}
