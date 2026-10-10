/*
 * LayoutConfiguration.c — 根目录 LAYOUT.CFG 读入并覆盖内建几何
 *
 * 【初学者】
 * - 分层：Core/Gui；与 Layout.c 共享 gWin / 栏高 / 图标步长（LayoutPrivate.h）
 * - 对外：LayoutLoadConfiguration
 * - 不做：按分辨率缩放（Layout.c 的 FitScale）；绘制
 */
#include "LayoutPrivate.h"
#include "FatFile.h"
#include "FileSystem.h"
#include "HalSerial.h"
#include "SerialConfig.h"

#define CONFIG_PATH "LAYOUT.CFG"
#define CONFIG_MAX  512u

/*
 * KeyEq — 行首键名是否等于 Key 且紧跟 '='
 *
 * 谁调用：仅 ApplyConfigurationLine。
 */
static int KeyEq(const char *Line, const char *Key, const char **Val) {
    UINTN i = 0;
    while (Key[i] != 0) {
        if (Line[i] != Key[i]) {
            return 0;
        }
        i++;
    }
    if (Line[i] != '=') {
        return 0;
    }
    *Val = Line + i + 1;
    return 1;
}

/*
 * ParseU32 — 从 *S 读十进制无符号整数并前移 *S
 *
 * 谁调用：ParseWidthByHeight、ApplyConfigurationLine。
 * 返回：1 成功；0 非法或溢出（>10000）
 */
static int ParseU32(const char **S, UINT32 *Out) {
    UINT32 V = 0;
    int Dig = 0;
    const char *P = *S;
    while (*P >= '0' && *P <= '9') {
        V = V * 10u + (UINT32)(*P - '0');
        P++;
        Dig = 1;
        if (V > 10000u) {
            return 0;
        }
    }
    if (!Dig) {
        return 0;
    }
    *S = P;
    *Out = V;
    return 1;
}

/*
 * ParseWidthByHeight — 解析 WxH 尺寸对
 *
 * 谁调用：ApplyConfigurationLine（shell/about/… 行）。
 */
static int ParseWidthByHeight(const char *S, UINT32 *W, UINT32 *H) {
    if (!ParseU32(&S, W) || (*S != 'x' && *S != 'X')) {
        return 0;
    }
    S++;
    if (!ParseU32(&S, H)) {
        return 0;
    }
    return 1;
}

/*
 * ApplyWindowDesignSize — 按窗名写回 gWin 设计宽高
 *
 * 谁调用：ApplyConfigurationLine。
 */
static void ApplyWindowDesignSize(const char *Name, UINT32 W, UINT32 H) {
    int i;
    for (i = 0; i < GUI_WIN_COUNT; i++) {
        const char *N = gWin[i].Name;
        UINTN k = 0;
        int Eq = 1;
        while (Name[k] != 0 || N[k] != 0) {
            if (Name[k] != N[k]) {
                Eq = 0;
                break;
            }
            k++;
        }
        if (Eq) {
            gWin[i].DesignW = W;
            gWin[i].DesignH = H;
            return;
        }
    }
}

/*
 * ApplyConfigurationLine — 解析单行 key=value 并改共享设计字段
 *
 * 谁调用：LayoutLoadConfiguration 逐行。
 */
static void ApplyConfigurationLine(const char *Line) {
    const char *Val = 0;
    UINT32 A;
    UINT32 B;

    while (*Line == ' ' || *Line == '\t') {
        Line++;
    }
    if (*Line == 0 || *Line == '#') {
        return;
    }
    if (KeyEq(Line, "bar_h", &Val) && ParseU32(&Val, &A)) {
        gBarH = A;
        return;
    }
    if (KeyEq(Line, "title_h", &Val) && ParseU32(&Val, &A)) {
        gTitleH = A;
        return;
    }
    if (KeyEq(Line, "icon_tile", &Val) && ParseU32(&Val, &A)) {
        gIconTile = A;
        return;
    }
    if (KeyEq(Line, "icon_stride", &Val) && ParseU32(&Val, &A)) {
        gIconStride = A;
        return;
    }
    if (KeyEq(Line, "shell", &Val) && ParseWidthByHeight(Val, &A, &B)) {
        ApplyWindowDesignSize("shell", A, B);
        return;
    }
    if (KeyEq(Line, "about", &Val) && ParseWidthByHeight(Val, &A, &B)) {
        ApplyWindowDesignSize("about", A, B);
        return;
    }
    if (KeyEq(Line, "settings", &Val) && ParseWidthByHeight(Val, &A, &B)) {
        ApplyWindowDesignSize("settings", A, B);
        return;
    }
    if (KeyEq(Line, "files", &Val) && ParseWidthByHeight(Val, &A, &B)) {
        ApplyWindowDesignSize("files", A, B);
        return;
    }
}

/*
 * LayoutLoadConfiguration — 从 FAT 根读 LAYOUT.CFG 覆盖内建表
 *
 * 做什么：有 OS 卷则读 CONFIG_PATH；失败保留 Layout.c 内建 1280×720 表。
 * 谁调用：GuiInitialize（WindowSetFb 之前已 LayoutSetFb）。
 * 前后文：
 *   前 — LayoutSetFb；FileSystemHasOsMarker
 *   后 — WindowLayoutAll / LayoutResolveWindow 用新设计值
 *   兄弟 — ThemeLoadConfiguration（THEME.CFG）
 * 返回：0 成功；-1 无卷/无文件/读失败（非致命，串口提示）
 */
int LayoutLoadConfiguration(void) {
    char Buf[CONFIG_MAX];
    UINT32 Size = 0;
    UINT32 i;
    char Line[80];
    UINTN L = 0;

    if (!FileSystemHasOsMarker()) {
        return -1;
    }
    if (FatFileReadPath(CONFIG_PATH, Buf, sizeof(Buf) - 1u, &Size) < 0 ||
        Size == 0) {
        HalSerialWriteChannel(SLOG_GUI, "Layout: cfg miss (builtin)\n");
        return -1;
    }
    Buf[Size] = 0;
    for (i = 0; i <= Size; i++) {
        char C = (i < Size) ? Buf[i] : '\n';
        if (C == '\n' || C == '\r' || i == Size) {
            if (L > 0) {
                Line[L] = 0;
                ApplyConfigurationLine(Line);
                L = 0;
            }
            continue;
        }
        if (L + 1u < sizeof(Line)) {
            Line[L++] = C;
        }
    }
    HalSerialWriteChannel(SLOG_GUI, "Layout: cfg loaded\n");
    return 0;
}
