/*
 * Theme.c — K21 色板 + K34 Settings 写回
 *
 * 【初学者】
 * 默认「墨色」：暖炭桌面 + 米白字，避开现网 tech 青蓝霓虹。
 * 落盘见 ThemeCfg.c（K37）。
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
static char gThemeName[8] = "ink";

static UINT32 Darken(UINT32 C) {
    UINT32 R = (C >> 16) & 0xFFu;
    UINT32 G = (C >> 8) & 0xFFu;
    UINT32 B = C & 0xFFu;
    R = (R * 3u) / 4u;
    G = (G * 3u) / 4u;
    B = (B * 3u) / 4u;
    return (R << 16) | (G << 8) | B;
}

/* 墨色：低饱和暖暗底，字用米白提高可读 */
static void ApplyInk(void) {
    gDesktopBg = 0x001C1B1Au;
    gTaskbarBg = 0x002C2A28u;
    gTitleFg = 0x00F2EDE6u;
    gTextFg = 0x00F2EDE6u;
    gWinTitleBar = 0x004A4540u;
    gWinTitleDim = Darken(gWinTitleBar);
    gWinClient = 0x00141210u;
    gWinBorder = 0x006B6560u;
}

/* 石板：中性灰，无青光 */
static void ApplySlate(void) {
    gDesktopBg = 0x0022262Bu;
    gTaskbarBg = 0x00303840u;
    gTitleFg = 0x00EEF1F4u;
    gTextFg = 0x00EEF1F4u;
    gWinTitleBar = 0x00485058u;
    gWinTitleDim = Darken(gWinTitleBar);
    gWinClient = 0x0016181Cu;
    gWinBorder = 0x00687078u;
}

/* 松烟：极低饱和绿灰 */
static void ApplyPine(void) {
    gDesktopBg = 0x001A201Cu;
    gTaskbarBg = 0x00283028u;
    gTitleFg = 0x00E8F0EAu;
    gTextFg = 0x00E8F0EAu;
    gWinTitleBar = 0x003E4A40u;
    gWinTitleDim = Darken(gWinTitleBar);
    gWinClient = 0x00121412u;
    gWinBorder = 0x00586058u;
}

void ThemeInitialize(void) {
    if (gReady) {
        return;
    }
    ApplyInk();
    gReady = 1;
    HalSerialWriteChannel(TOY_SLOG_GUI, "Theme: palette ok (ink)\n");
}

int ThemeApplyNamed(const char *Name) {
    if (Name == 0) {
        return -1;
    }
    if (!gReady) {
        ThemeInitialize();
    }
    if (Name[0] == 'i' && Name[1] == 'n' && Name[2] == 'k' && Name[3] == 0) {
        ApplyInk();
        gThemeName[0] = 'i';
        gThemeName[1] = 'n';
        gThemeName[2] = 'k';
        gThemeName[3] = 0;
        HalSerialWriteChannel(TOY_SLOG_GUI, "Theme: ink\n");
        return 0;
    }
    if (Name[0] == 's' && Name[1] == 'l' && Name[2] == 'a' && Name[3] == 't' &&
        Name[4] == 'e' && Name[5] == 0) {
        ApplySlate();
        gThemeName[0] = 's';
        gThemeName[1] = 'l';
        gThemeName[2] = 'a';
        gThemeName[3] = 't';
        gThemeName[4] = 'e';
        gThemeName[5] = 0;
        HalSerialWriteChannel(TOY_SLOG_GUI, "Theme: slate\n");
        return 0;
    }
    if (Name[0] == 'p' && Name[1] == 'i' && Name[2] == 'n' && Name[3] == 'e' &&
        Name[4] == 0) {
        ApplyPine();
        gThemeName[0] = 'p';
        gThemeName[1] = 'i';
        gThemeName[2] = 'n';
        gThemeName[3] = 'e';
        gThemeName[4] = 0;
        HalSerialWriteChannel(TOY_SLOG_GUI, "Theme: pine\n");
        return 0;
    }
    /* tech/modern：刻意不提供；青蓝霓虹课感差 */
    return -1;
}

const char *ThemeName(void) {
    if (!gReady) {
        ThemeInitialize();
    }
    return gThemeName;
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
