/*
 * ShellCommandsFontProbe.c — `fonts probe [cold] <text>...` 注册
 *
 * 测量/打印 → FontProbeMeasure.c / FontProbeFormat.c（PR-NORM-16）。
 */
#include "ShellPrivate.h"
#include "Console.h"

void ShellCommandFonts(int Argc, char **Argv);

void ShellCommandsFontProbeRegister(void) {
    gConsole.Register("fonts", "fonts probe [cold] <text>... (adv/ink, read-only)",
                      ShellCommandFonts);
}
