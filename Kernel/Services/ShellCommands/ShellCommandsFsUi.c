/*
 * ShellCommandsFsUi.c — PR-S3-shellfsui-1：开窗 / font / lang + 注册汇总
 *
 * store 见 ShellCommandsFsUiStore{,.Job}.c。
 */
#include "ShellPrivate.h"
#include "Console.h"
#include "Gui.h"
#include "Locale.h"
#include "Font.h"
#include "Theme.h"

static void CommandShell(int Argc, char **Argv) {
    int Idx;

    (void)Argc;
    (void)Argv;
    Idx = GuiOpenShell();
    if (Idx < 0) {
        gConsole.Write("shell: no free window\n");
        return;
    }
    /* 欢迎语已在 GuiOpenShell 淡入前画好 */
}

static void CommandSettings(int Argc, char **Argv) {
    int Idx;

    (void)Argc;
    (void)Argv;
    Idx = GuiOpenSettings();
    if (Idx < 0) {
        gConsole.Write("settings: no free window\n");
    }
}

static void CommandFiles(int Argc, char **Argv) {
    int Idx;

    (void)Argc;
    (void)Argv;
    Idx = GuiOpenFiles();
    if (Idx < 0) {
        gConsole.Write("files: no free window\n");
    }
}

static void CommandEdit(int Argc, char **Argv) {
    int Idx;
    const char *Path;

    if (Argc < 2 || !Argv[1] || !Argv[1][0]) {
        gConsole.Write("usage: edit <path>\n");
        return;
    }
    Path = Argv[1];
    Idx = GuiOpenEdit(Path);
    if (Idx < 0) {
        gConsole.Write("edit: no free window\n");
    }
}

static void CommandTty(int Argc, char **Argv) {
    int Idx;

    (void)Argc;
    (void)Argv;
    Idx = GuiOpenTty();
    if (Idx < 0) {
        gConsole.Write("tty: no free window\n");
    }
}

static void CommandZh(int Argc, char **Argv) {
    const char *S;
    UINT32 Cp;
    UINTN N;
    UINT32 W;
    UINT32 H;
    const UINT8 *Pix;

    (void)Argc;
    (void)Argv;
    gConsole.Write("你好，世界！中文测试\n");
    S = "一你好测";
    while (*S) {
        N = Utf8Decode(S, &Cp);
        if (N == 0) {
            break;
        }
        S += N;
        if (Cp < 128u) {
            continue;
        }
        Pix = FontTtfCacheGet(Cp, &W, &H);
        gConsole.Write("ttf U+");
        gConsole.WriteHex32(Cp);
        if (Pix) {
            gConsole.Write(" ok\n");
        } else {
            gConsole.Write(" miss\n");
        }
        (void)W;
        (void)H;
    }
}

static void CommandFont(int Argc, char **Argv) {
    UINT32 i;
    UINT32 Id;
    const FONT_FACE *F;

    if (Argc >= 2 && Argv[1][0] == 'r' && Argv[1][1] == 'e' &&
        Argv[1][2] == 'l' && Argv[1][3] == 'o' && Argv[1][4] == 'a' &&
        Argv[1][5] == 'd' && Argv[1][6] == 0) {
        (void)FontReloadAssets();
        ThemeClampFontId();
        gConsole.Write("Font: assets reloaded\n");
        return;
    }
    if (Argc >= 2 && Argv[1][0] >= '0' && Argv[1][0] <= '9') {
        Id = 0;
        for (i = 0; Argv[1][i] >= '0' && Argv[1][i] <= '9'; i++) {
            Id = Id * 10u + (UINT32)(Argv[1][i] - '0');
        }
        if (Argv[1][i] != 0 || ThemeSetFontId(Id) != 0) {
            gConsole.Write("Font: bad id\n");
            return;
        }
        ThemeApply();
        gConsole.Write("Font: set ");
        F = FontGetCurrent();
        gConsole.Write(F && F->Name ? F->Name : "?");
        gConsole.Write("\n");
        return;
    }
    gConsole.Write("fonts:\n");
    for (i = 0; i < FontCount(); i++) {
        F = FontGetById(i);
        gConsole.Write(i == FontCurrentId() ? " * " : "   ");
        gConsole.Write(F && F->Name ? F->Name : "?");
        gConsole.Write("\n");
    }
    if (Argc < 2) {
        gConsole.Write("usage: font [reload|<id>]\n");
    }
}

static void CommandLang(int Argc, char **Argv) {
    if (Argc < 2) {
        gConsole.Write(LocStr(MSG_LANG_USAGE));
        gConsole.Write(LocStr(MSG_LANG_NOW));
        gConsole.Write(LocaleGet() == LOC_LANG_ZH ? "zh\n" : "en\n");
        return;
    }
    if (Argv[1][0] == 'r' && Argv[1][1] == 'e' && Argv[1][2] == 'l' &&
        Argv[1][3] == 'o' && Argv[1][4] == 'a' && Argv[1][5] == 'd' &&
        Argv[1][6] == 0) {
        LocaleReload();
        gConsole.Write("locale reloaded\n");
        return;
    }
    if (Argv[1][0] == 'z' && Argv[1][1] == 'h') {
        (void)LocaleSet(LOC_LANG_ZH);
        gConsole.Write(LocStr(MSG_LANG_SET));
        return;
    }
    if (Argv[1][0] == 'e' && Argv[1][1] == 'n') {
        (void)LocaleSet(LOC_LANG_EN);
        gConsole.Write(LocStr(MSG_LANG_SET));
        return;
    }
    gConsole.Write(LocStr(MSG_LANG_BAD));
}

void ShellCommandsFsUiRegister(void) {
    gConsole.Register2("test", "glyph", "UTF-8 Chinese glyph test", CommandZh);
    gConsole.RegisterAliasLine("zh", "test", "glyph");

    gConsole.Register2("set", "language", "set language en|zh|reload", CommandLang);
    gConsole.RegisterAliasLine("lang", "set", "language");
    gConsole.RegisterAliasLine("language", "set", "language");

    gConsole.Register("shell", "open Shell window", CommandShell);
    gConsole.Register("settings", "open Settings window", CommandSettings);
    gConsole.Register("files", "open Files browser", CommandFiles);
    gConsole.Register("edit", "edit <path> open text editor (PR-V2)", CommandEdit);
    gConsole.Register("tty", "open serial TTY session window", CommandTty);
    gConsole.Register("font", "font [reload|<id>] (Assets/Fonts TOYF)", CommandFont);
    ShellCommandsFsUiStoreRegister();
}
