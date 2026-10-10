/*
 * ThemeConfiguration.c — K37：THEME.CFG 读色/mode、写回 FAT
 *
 * 【初学者】
 * - Core/Video：持久化 Theme.c 色板与 GOP 偏好（mode=）。
 * - 入口：ThemeLoadConfiguration、ThemeSaveConfiguration、ThemeGetMode、ThemeSetMode。
 * - 边界：键值 desktop/title/taskbar/theme/mode；不热切 GOP（Boot VideoTheme 读 mode）。
 */
#include "Theme.h"
#include "FatFile.h"
#include "FileSystem.h"
#include "HalSerial.h"
#include "SerialConfig.h"

#define CFG_PATH "THEME.CFG"
#define CFG_MAX  512u

/* 0,0 = auto（未偏好或显式 auto） */
static UINT32 gModeW;
static UINT32 gModeH;

static int IsHex(char C) {
    return (C >= '0' && C <= '9') || (C >= 'a' && C <= 'f') ||
           (C >= 'A' && C <= 'F');
}

static UINT32 HexVal(char C) {
    if (C >= '0' && C <= '9') {
        return (UINT32)(C - '0');
    }
    if (C >= 'a' && C <= 'f') {
        return (UINT32)(C - 'a' + 10);
    }
    if (C >= 'A' && C <= 'F') {
        return (UINT32)(C - 'A' + 10);
    }
    return 0;
}

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

static int ParseHexColor(const char *S, UINT32 *Out) {
    UINT32 V = 0;
    UINTN N = 0;

    if (S == 0 || Out == 0) {
        return 0;
    }
    if (S[0] == '0' && (S[1] == 'x' || S[1] == 'X')) {
        S += 2;
    }
    while (IsHex(*S) && N < 6u) {
        V = (V << 4) | HexVal(*S);
        S++;
        N++;
    }
    if (N == 0) {
        return 0;
    }
    *Out = V & 0x00FFFFFFu;
    return 1;
}

static int ParseMode(const char *S, UINT32 *W, UINT32 *H) {
    UINT32 Ww = 0;
    UINT32 Hh = 0;

    if (S == 0 || W == 0 || H == 0) {
        return 0;
    }
    if (S[0] == 'a' || S[0] == 'A') {
        *W = 0;
        *H = 0;
        return 1;
    }
    while (*S >= '0' && *S <= '9') {
        Ww = Ww * 10u + (UINT32)(*S - '0');
        S++;
        if (Ww > 10000u) {
            return 0;
        }
    }
    if (*S != 'x' && *S != 'X') {
        return 0;
    }
    S++;
    while (*S >= '0' && *S <= '9') {
        Hh = Hh * 10u + (UINT32)(*S - '0');
        S++;
        if (Hh > 10000u) {
            return 0;
        }
    }
    if (Ww < 640u || Hh < 480u) {
        return 0;
    }
    *W = Ww;
    *H = Hh;
    return 1;
}

/*
 * ApplyLine — 解析一行 THEME.CFG
 *
 * 做什么：识别 desktop/title/taskbar/mode/theme= 并调 ThemeSet* / ThemeApplyNamed。
 * 谁调用：仅 ThemeLoadConfiguration 行循环。
 * 前后文：兄弟 — ParseHexColor、ParseMode、KeyEq。
 */
static void ApplyLine(const char *Line) {
    const char *Val = 0;
    UINT32 C;
    UINT32 W;
    UINT32 H;

    while (*Line == ' ' || *Line == '\t') {
        Line++;
    }
    if (*Line == 0 || *Line == '#') {
        return;
    }
    if (KeyEq(Line, "desktop", &Val) && ParseHexColor(Val, &C)) {
        ThemeSetDesktopBackground(C);
        return;
    }
    if (KeyEq(Line, "title", &Val) && ParseHexColor(Val, &C)) {
        ThemeSetWindowTitleBar(C);
        return;
    }
    if (KeyEq(Line, "taskbar", &Val) && ParseHexColor(Val, &C)) {
        ThemeSetTaskbarBackground(C);
        return;
    }
    if (KeyEq(Line, "mode", &Val) && ParseMode(Val, &W, &H)) {
        gModeW = W;
        gModeH = H;
        return;
    }
    if (KeyEq(Line, "theme", &Val)) {
        char Name[8];
        UINTN i = 0;
        while (Val[i] != 0 && Val[i] != '\n' && Val[i] != '\r' &&
               Val[i] != ' ' && i + 1u < sizeof(Name)) {
            Name[i] = Val[i];
            i++;
        }
        Name[i] = 0;
        if (ThemeApplyNamed(Name) != 0) {
            /* 忽略 tech/未知，保持当前色 */
        }
        return;
    }
}

static void Hex6(char *Dst, UINT32 C) {
    static const char Dig[] = "0123456789abcdef";
    UINT32 i;

    C &= 0x00FFFFFFu;
    for (i = 0; i < 6u; i++) {
        Dst[5u - i] = Dig[C & 0xFu];
        C >>= 4;
    }
    Dst[6] = 0;
}

static UINTN Append(char *Buf, UINTN Cap, UINTN Pos, const char *S) {
    while (*S != 0 && Pos + 1u < Cap) {
        Buf[Pos++] = *S++;
    }
    Buf[Pos] = 0;
    return Pos;
}

/*
 * ThemeGetMode — 读持久化 GOP 偏好
 *
 * 做什么：输出 gModeW/gModeH；0,0 表示 auto。
 * 谁调用：Shell `theme mode`、ThemeSaveConfiguration。
 * 前后文：前 — ThemeSetMode 或 cfg 加载；后 — Boot 侧 VideoTheme（非本文件）。
 * 返回：void（指针可 NULL 侧写忽略）。
 */
void ThemeGetMode(UINT32 *W, UINT32 *H) {
    if (W != 0) {
        *W = gModeW;
    }
    if (H != 0) {
        *H = gModeH;
    }
}

/*
 * ThemeSetMode — 写 GOP 偏好（内存，落盘靠 Save）
 *
 * 做什么：W×H≥640×480 或 auto(0,0)；非法尺寸忽略。
 * 谁调用：Shell `theme mode`（ShellCommand/Theme.c）。
 * 前后文：后 — ThemeSaveConfiguration 写 mode= 行。
 */
void ThemeSetMode(UINT32 W, UINT32 H) {
    if (W == 0 || H == 0) {
        gModeW = 0;
        gModeH = 0;
        return;
    }
    if (W < 640u || H < 480u) {
        return;
    }
    gModeW = W;
    gModeH = H;
}

/*
 * ThemeLoadConfiguration — 从 FAT 读 THEME.CFG
 *
 * 做什么：FatFileReadPath → 逐行 ApplyLine；无 FS/无文件仍返回 -1 但 ThemeInitialize 已跑。
 * 谁调用：GuiInitialize（K37，失败则用出厂色）。
 * 前后文：前 — FileSystemHasOsMarker；兄弟 — LayoutConfiguration 读 LAYOUT.CFG。
 * 返回：0 成功；-1 跳过或缺失。
 */
int ThemeLoadConfiguration(void) {
    char Buf[CFG_MAX];
    UINT32 Size = 0;
    UINT32 i;
    char Line[80];
    UINTN L = 0;

    ThemeInitialize();
    gModeW = 0;
    gModeH = 0;
    if (!FileSystemHasOsMarker()) {
        HalSerialWriteChannel(SLOG_GUI, "Theme: cfg skip (no fs)\n");
        return -1;
    }
    if (FatFileReadPath(CFG_PATH, Buf, sizeof(Buf) - 1u, &Size) < 0 ||
        Size == 0) {
        HalSerialWriteChannel(SLOG_GUI, "Theme: cfg miss\n");
        return -1;
    }
    Buf[Size] = 0;
    for (i = 0; i <= Size; i++) {
        char C = (i < Size) ? Buf[i] : '\n';
        if (C == '\n' || C == '\r' || i == Size) {
            if (L > 0) {
                Line[L] = 0;
                ApplyLine(Line);
                L = 0;
            }
            continue;
        }
        if (L + 1u < sizeof(Line)) {
            Line[L++] = C;
        }
    }
    HalSerialWriteChannel(SLOG_GUI, "Theme: cfg loaded\n");
    return 0;
}

/*
 * ThemeSaveConfiguration — 把当前色板/mode 写 THEME.CFG
 *
 * 做什么：拼文本行 FatFileWritePath；含 theme/desktop/title/taskbar/mode。
 * 谁调用：Settings 改色后、Shell `theme mode`。
 * 前后文：前 — ThemeSet* / ThemeApplyNamed；后 — 下次 ThemeLoadConfiguration。
 * 返回：0 成功；-1 无 FS 或写失败。
 */
int ThemeSaveConfiguration(void) {
    char Buf[CFG_MAX];
    char Hex[8];
    UINTN Pos = 0;
    char ModeLine[40];
    UINTN i;
    UINT32 W;
    UINT32 H;

    ThemeInitialize();
    if (!FileSystemHasOsMarker()) {
        HalSerialWriteChannel(SLOG_GUI, "Theme: save skip (no fs)\n");
        return -1;
    }
    Pos = Append(Buf, sizeof(Buf), Pos, "# Blocks THEME.CFG\n");
    Pos = Append(Buf, sizeof(Buf), Pos, "theme=");
    Pos = Append(Buf, sizeof(Buf), Pos, ThemeName());
    Pos = Append(Buf, sizeof(Buf), Pos, "\n");
    Hex6(Hex, ThemeDesktopBackground());
    Pos = Append(Buf, sizeof(Buf), Pos, "desktop=");
    Pos = Append(Buf, sizeof(Buf), Pos, Hex);
    Pos = Append(Buf, sizeof(Buf), Pos, "\n");
    Hex6(Hex, ThemeWindowTitleBar());
    Pos = Append(Buf, sizeof(Buf), Pos, "title=");
    Pos = Append(Buf, sizeof(Buf), Pos, Hex);
    Pos = Append(Buf, sizeof(Buf), Pos, "\n");
    Hex6(Hex, ThemeTaskbarBackground());
    Pos = Append(Buf, sizeof(Buf), Pos, "taskbar=");
    Pos = Append(Buf, sizeof(Buf), Pos, Hex);
    Pos = Append(Buf, sizeof(Buf), Pos, "\n");
    ThemeGetMode(&W, &H);
    if (W == 0 || H == 0) {
        Pos = Append(Buf, sizeof(Buf), Pos, "mode=auto\n");
    } else {
        /* 手写十进制，避免依赖 printf */
        i = 0;
        ModeLine[i++] = 'm';
        ModeLine[i++] = 'o';
        ModeLine[i++] = 'd';
        ModeLine[i++] = 'e';
        ModeLine[i++] = '=';
        {
            char Tmp[8];
            int N = 0;
            UINT32 X = W;
            do {
                Tmp[N++] = (char)('0' + (X % 10u));
                X /= 10u;
            } while (X != 0 && N < 8);
            while (N > 0) {
                ModeLine[i++] = Tmp[--N];
            }
        }
        ModeLine[i++] = 'x';
        {
            char Tmp[8];
            int N = 0;
            UINT32 X = H;
            do {
                Tmp[N++] = (char)('0' + (X % 10u));
                X /= 10u;
            } while (X != 0 && N < 8);
            while (N > 0) {
                ModeLine[i++] = Tmp[--N];
            }
        }
        ModeLine[i++] = '\n';
        ModeLine[i] = 0;
        Pos = Append(Buf, sizeof(Buf), Pos, ModeLine);
    }
    if (FatFileWritePath(CFG_PATH, Buf, (UINT32)Pos) != 0) {
        HalSerialWriteChannel(SLOG_GUI, "Theme: save fail\n");
        return -1;
    }
    HalSerialWriteChannel(SLOG_GUI, "Theme: cfg saved\n");
    return 0;
}
