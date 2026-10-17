/*
 * ShellCommandsTheme.c — set effects（PR-GUI-effects）
 */
#include "ShellPrivate.h"
#include "Console.h"
#include "Theme.h"

static int EffectsNameEq(const char *A, const char *B) {
    int i;

    if (!A || !B) {
        return 0;
    }
    for (i = 0; A[i] || B[i]; i++) {
        if (A[i] != B[i]) {
            return 0;
        }
    }
    return 1;
}

/* set effects minimal|low|medium|high */
static void CommandSetEffects(int Argc, char **Argv) {
    THEME_EFFECT_LEVEL Level;

    if (Argc < 2) {
        gConsole.Write("usage: set effects minimal|low|medium|high\n");
        gConsole.Write("  now=");
        gConsole.Write(ThemeEffectLevelName(ThemeGetEffectLevel()));
        gConsole.Write("\n");
        return;
    }
    if (EffectsNameEq(Argv[1], "minimal")) {
        Level = THEME_EFFECT_MINIMAL;
    } else if (EffectsNameEq(Argv[1], "low")) {
        Level = THEME_EFFECT_LOW;
    } else if (EffectsNameEq(Argv[1], "medium")) {
        Level = THEME_EFFECT_MEDIUM;
    } else if (EffectsNameEq(Argv[1], "high")) {
        Level = THEME_EFFECT_HIGH;
    } else {
        gConsole.Write("set effects: bad level\n");
        return;
    }
    ThemeSetEffectLevel(Level);
    ThemeApply();
    gConsole.Write("set effects: ");
    gConsole.Write(ThemeEffectLevelName(Level));
    gConsole.Write("\n");
}

void ShellCommandsThemeRegister(void) {
    gConsole.Register2("set", "effects", "set effects minimal|low|medium|high",
                     CommandSetEffects);
}
