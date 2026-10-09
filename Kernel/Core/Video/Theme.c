/*
 * Theme.c — K21 色板 + K34 Settings 写回
 *
 * 【初学者】
 * 出厂色保持课感；Settings 改内存色板，THEME.CFG 落盘另刀。
 */
#include "Theme.h"
#include "HalSerial.h"
#include "ToySerialConfig.h"

static UINT32 gDesktopBg;
static UINT32 gTaskbarBg;
static UINT32 gTitleFg;
static UINT32 gTextFg;
static UINT32 gWinTitleBar;
static UINT32 gWinTitleDim;
static UINT32 gWinClient;
static UINT32 gWinBorder;
static int gReady;

static UINT32 Darken(UINT32 C) {
    UINT32 R = (C >> 16) & 0xFFu;
    UINT32 G = (C >> 8) & 0xFFu;
    UINT32 B = C & 0xFFu;
    R = (R * 3u) / 4u;
    G = (G * 3u) / 4u;
    B = (B * 3u) / 4u;
    return (R << 16) | (G << 8) | B;
}

void ThemeInitialize(void) {
    if (gReady) {
        return;
    }
    gDesktopBg = 0x001A1F24u;
    gTaskbarBg = 0x002A323Cu;
    gTitleFg = 0x00E8EEF4u;
    gTextFg = 0x00FFFFFFu;
    gWinTitleBar = 0x003D4F5Fu;
    gWinTitleDim = Darken(gWinTitleBar);
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
    return gWinTitleDim;
}

void ThemeSetDesktopBackground(UINT32 Color) {
    if (!gReady) {
        ThemeInitialize();
    }
    gDesktopBg = Color & 0x00FFFFFFu;
    HalSerialWriteChannel(TOY_SLOG_GUI, "Theme: desktop set\n");
}

void ThemeSetWindowTitleBar(UINT32 Color) {
    if (!gReady) {
        ThemeInitialize();
    }
    gWinTitleBar = Color & 0x00FFFFFFu;
    gWinTitleDim = Darken(gWinTitleBar);
    HalSerialWriteChannel(TOY_SLOG_GUI, "Theme: title set\n");
}

void ThemeSetTaskbarBackground(UINT32 Color) {
    if (!gReady) {
        ThemeInitialize();
    }
    gTaskbarBg = Color & 0x00FFFFFFu;
    HalSerialWriteChannel(TOY_SLOG_GUI, "Theme: taskbar set\n");
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
