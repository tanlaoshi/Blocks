/*
 * Theme.c — Shell：mode / lang
 *
 * 【初学者】
 * - 分层：Core/Console/ShellCommand；持久化 ThemeSaveConfiguration
 * - 对外入口：ThemeRegister
 * - 不做：即时改分辨率（mode 下回启动生效）
 */
#include "ShellCommand.h"
#include "Locale.h"
#include "Gui.h"
#include "Console.h"
#include "Theme.h"
#include "HalSerial.h"

static void Put(const char *S) {
    HalSerialWriteShell(S);
}

static int StringsEqual(const char *A, const char *B) {
    if (A == 0 || B == 0) {
        return 0;
    }
    while (*A && *B && *A == *B) {
        A++;
        B++;
    }
    return *A == 0 && *B == 0;
}

static void PutUnsigned32(UINT32 V) {
    char Digits[12];
    int Count = 0;
    int Index;

    if (V == 0) {
        Put("0");
        return;
    }
    while (V != 0 && Count < 11) {
        Digits[Count++] = (char)('0' + (V % 10u));
        V /= 10u;
    }
    for (Index = Count - 1; Index >= 0; Index--) {
        char One[2];
        One[0] = Digits[Index];
        One[1] = 0;
        Put(One);
    }
}

static void CommandMode(int Argc, char **Argv) {
    UINT32 W = 0;
    UINT32 H = 0;
    const char *S;
    UINT32 Ww = 0;
    UINT32 Hh = 0;

    ThemeGetMode(&W, &H);
    if (Argc < 2) {
        Put("mode: ");
        if (W == 0 || H == 0) {
            Put("auto");
        } else {
            PutUnsigned32(W);
            Put("x");
            PutUnsigned32(H);
        }
        Put(" (next boot; ThemeSave keeps it)\n");
        return;
    }
    S = Argv[1];
    if (StringsEqual(S, "auto")) {
        ThemeSetMode(0, 0);
        if (ThemeSaveConfiguration() != 0) {
            Put("mode: save fail\n");
            return;
        }
        Put("mode: auto (saved)\n");
        return;
    }
    while (*S >= '0' && *S <= '9') {
        Ww = Ww * 10u + (UINT32)(*S - '0');
        S++;
        if (Ww > 10000u) {
            Put("mode: bad (use 1024x768|auto)\n");
            return;
        }
    }
    if (*S != 'x' && *S != 'X') {
        Put("mode: bad (use 1024x768|auto)\n");
        return;
    }
    S++;
    while (*S >= '0' && *S <= '9') {
        Hh = Hh * 10u + (UINT32)(*S - '0');
        S++;
        if (Hh > 10000u) {
            Put("mode: bad (use 1024x768|auto)\n");
            return;
        }
    }
    if (*S != 0 || Ww < 640u || Hh < 480u) {
        Put("mode: bad (min 640x480)\n");
        return;
    }
    ThemeSetMode(Ww, Hh);
    if (ThemeSaveConfiguration() != 0) {
        Put("mode: save fail\n");
        return;
    }
    Put("mode: saved ");
    PutUnsigned32(Ww);
    Put("x");
    PutUnsigned32(Hh);
    Put(" (reboot to apply)\n");
}

static void CommandLang(int Argc, char **Argv) {
    if (Argc < 2) {
        Put(LocStr(MSG_LANG_USAGE));
        Put("\n");
        Put(LocStr(MSG_LANG_NOW));
        return;
    }
    if (StringsEqual(Argv[1], "en")) {
        (void)LocaleSet(LOC_LANG_EN);
        GuiRefreshLabels();
        ConsoleRefreshBanner();
        Put(LocStr(MSG_LANG_SET));
        return;
    }
    if (StringsEqual(Argv[1], "zh")) {
        (void)LocaleSet(LOC_LANG_ZH);
        GuiRefreshLabels();
        ConsoleRefreshBanner();
        Put(LocStr(MSG_LANG_SET));
        return;
    }
    Put(LocStr(MSG_LANG_USAGE));
    Put("\n");
}

/*
 * ThemeRegister — 注册 lang / mode
 *
 * 谁调用：ShellCommandInitialize。
 * 返回：void
 */
void ThemeRegister(void) {
    ShellCommandRegister("lang", "UI language en|zh", CommandLang);
    ShellCommandRegister("mode", "display pref WxH|auto", CommandMode);
}
