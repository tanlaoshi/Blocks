/*
 * Theme.c — K21 最小色板
 *
 * 【初学者】
 * 出厂色与旧 Gui 硬编码一致，避免本刀「换色惊吓」。
 * 后刀 Settings / THEME.CFG / DB 再写回这些值。
 */
#include "Theme.h"
#include "HalSerial.h"
#include "ToySerialConfig.h"

static UINT32 gDesktopBg;
static UINT32 gTaskbarBg;
static UINT32 gTitleFg;
static UINT32 gTextFg;
static UINT32 gWinTitleBar;
static UINT32 gWinClient;
static UINT32 gWinBorder;
static int gReady;

void ThemeInitialize(void) {
    if (gReady) {
        return;
    }
    gDesktopBg = 0x001A1F24u;
    gTaskbarBg = 0x002A323Cu;
    gTitleFg = 0x00E8EEF4u;
    gTextFg = 0x00FFFFFFu;
    gWinTitleBar = 0x003D4F5Fu;
    gWinClient = 0x00101214u;
    gWinBorder = 0x005A6A78u;
    gReady = 1;
    HalSerialWriteChannel(TOY_SLOG_GUI, "Theme: palette ok\n");
}

UINT32 ThemeDesktopBackground(void) {
    if (!gReady) {
        ThemeInitialize();
    }
    return gDesktopBg;
}

UINT32 ThemeTaskbarBackground(void) {
    if (!gReady) {
        ThemeInitialize();
    }
    return gTaskbarBg;
}

UINT32 ThemeWindowTitleText(void) {
    if (!gReady) {
        ThemeInitialize();
    }
    return gTitleFg;
}

UINT32 ThemeTextForeground(void) {
    if (!gReady) {
        ThemeInitialize();
    }
    return gTextFg;
}

UINT32 ThemeWindowTitleBar(void) {
    if (!gReady) {
        ThemeInitialize();
    }
    return gWinTitleBar;
}

UINT32 ThemeWindowTitleBarDim(void) {
    if (!gReady) {
        ThemeInitialize();
    }
    /* 失焦：略压暗标题栏 */
    return 0x002A3540u;
}

UINT32 ThemeWindowClient(void) {
    if (!gReady) {
        ThemeInitialize();
    }
    return gWinClient;
}

UINT32 ThemeWindowBorder(void) {
    if (!gReady) {
        ThemeInitialize();
    }
    return gWinBorder;
}
