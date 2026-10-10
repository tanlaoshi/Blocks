/*
 * Theme.c — K21 内存色板（桌面/任务栏/窗口色）
 *
 * 【初学者】
 * - Core/Video：Gui/Console 读 Theme* 上色；Settings/Shell 改色后经 ThemeConfiguration 落盘。
 * - 入口：ThemeInitialize、ThemeApplyNamed、Theme*Background/ThemeSet*、ThemeName。
 * - 边界：不读 FAT；mode/GOP 见 ThemeConfiguration.c；不提供 tech 霓虹预设。
 */
#include "Theme.h"
#include "HalSerial.h"
#include "SerialConfig.h"

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

/*
 * ThemeInitialize — 默认「墨色」色板
 *
 * 做什么：ApplyInk，置 gReady；重复调用无操作。
 * 谁调用：GuiInitialize、ConsoleRefresh、ThemeLoadConfiguration、各 Theme* getter 懒初始化。
 * 前后文：后 — ThemeDesktopBackground 等读 g*；Settings 可 ThemeApplyNamed 覆盖。
 * 返回：void。
 */
void ThemeInitialize(void) {
    if (gReady) {
        return;
    }
    ApplyInk();
    gReady = 1;
    HalSerialWriteChannel(SLOG_GUI, "Theme: palette ok (ink)\n");
}

/*
 * ThemeApplyNamed — 按名切换 ink/slate/pine
 *
 * 做什么：匹配预设名并改 gThemeName 与色值；tech/modern 等返回失败。
 * 谁调用：Settings 桌面色块、ThemeConfiguration ApplyLine（theme= 行）。
 * 前后文：后 — ThemeSaveConfiguration 可把名写回 THEME.CFG。
 * 返回：0 成功；-1 未知名。
 */
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
        HalSerialWriteChannel(SLOG_GUI, "Theme: ink\n");
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
        HalSerialWriteChannel(SLOG_GUI, "Theme: slate\n");
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
        HalSerialWriteChannel(SLOG_GUI, "Theme: pine\n");
        return 0;
    }
    /* tech/modern：刻意不提供；青蓝霓虹课感差 */
    return -1;
}

/*
 * ThemeName — 当前预设名（如 "ink"）
 *
 * 做什么：返回 gThemeName；未就绪则 ThemeInitialize。
 * 谁调用：ThemeSaveConfiguration 写 theme= 行。
 * 返回：静态缓冲区指针。
 */
const char *ThemeName(void) {
    if (!gReady) {
        ThemeInitialize();
    }
    return gThemeName;
}

/*
 * ThemeDesktopBackground — 桌面填充色（0xRRGGBB）
 *
 * 做什么：读 gDesktopBg；懒 ThemeInitialize。
 * 谁调用：WindowPaint 清屏、ThemeSaveConfiguration。
 */
UINT32 ThemeDesktopBackground(void) {
    if (!gReady) {
        ThemeInitialize();
    }
    return gDesktopBg;
}

/* ThemeTaskbarBackground — 任务栏底色；Gui Layout/WindowPaint。 */
UINT32 ThemeTaskbarBackground(void) {
    if (!gReady) {
        ThemeInitialize();
    }
    return gTaskbarBg;
}

/* ThemeWindowTitleText — 标题栏前景字色；FontDrawStringAt 窗口标题。 */
UINT32 ThemeWindowTitleText(void) {
    if (!gReady) {
        ThemeInitialize();
    }
    return gTitleFg;
}

/* ThemeTextForeground — 正文前景；ConsoleRefresh 等。 */
UINT32 ThemeTextForeground(void) {
    if (!gReady) {
        ThemeInitialize();
    }
    return gTextFg;
}

/* ThemeWindowTitleBar — 活动窗口标题条底色。 */
UINT32 ThemeWindowTitleBar(void) {
    if (!gReady) {
        ThemeInitialize();
    }
    return gWinTitleBar;
}

/* ThemeWindowTitleBarDim — 非活动标题条（Darken 自 ThemeWindowTitleBar）。 */
UINT32 ThemeWindowTitleBarDim(void) {
    if (!gReady) {
        ThemeInitialize();
    }
    return gWinTitleDim;
}

/*
 * ThemeSetDesktopBackground — 改桌面色（0xRRGGBB）
 *
 * 做什么：掩码写 gDesktopBg；不自动落盘。
 * 谁调用：Settings 色块、ThemeConfiguration desktop= 行。
 * 前后文：后 — ThemeSaveConfiguration、Gui 重绘。
 */
void ThemeSetDesktopBackground(UINT32 Color) {
    if (!gReady) {
        ThemeInitialize();
    }
    gDesktopBg = Color & 0x00FFFFFFu;
    HalSerialWriteChannel(SLOG_GUI, "Theme: desktop set\n");
}

/*
 * ThemeSetWindowTitleBar — 改标题条色并重算 Dim
 *
 * 谁调用：Settings、ThemeConfiguration title= 行。
 */
void ThemeSetWindowTitleBar(UINT32 Color) {
    if (!gReady) {
        ThemeInitialize();
    }
    gWinTitleBar = Color & 0x00FFFFFFu;
    gWinTitleDim = Darken(gWinTitleBar);
    HalSerialWriteChannel(SLOG_GUI, "Theme: title set\n");
}

/*
 * ThemeSetTaskbarBackground — 改任务栏色
 *
 * 谁调用：Settings、ThemeConfiguration taskbar= 行。
 */
void ThemeSetTaskbarBackground(UINT32 Color) {
    if (!gReady) {
        ThemeInitialize();
    }
    gTaskbarBg = Color & 0x00FFFFFFu;
    HalSerialWriteChannel(SLOG_GUI, "Theme: taskbar set\n");
}

/* ThemeWindowClient — 窗口客户区底色；WindowPaint。 */
UINT32 ThemeWindowClient(void) {
    if (!gReady) {
        ThemeInitialize();
    }
    return gWinClient;
}

/* ThemeWindowBorder — 窗口边框色；WindowPaint。 */
UINT32 ThemeWindowBorder(void) {
    if (!gReady) {
        ThemeInitialize();
    }
    return gWinBorder;
}
