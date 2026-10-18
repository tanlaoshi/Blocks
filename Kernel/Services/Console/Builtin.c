/*
 * Builtin.c — help / clear / echo 与内置注册
 * 核心：Command.c
 */
#include "Console.h"
#include "ConsolePrivate.h"
#include "Gui.h"
#include "Hal.h"
#include "Db.h"
#include "ShellCommands.h"

static void HelpWriteAliasesFor(const char *Level1, const char *Level2) {
    int First = 1;
    int i;

    for (i = 0; i < gUserAliasCount; i++) {
        if (gUserAliases[i].Alias[0] != 0 &&
            StringEqual(gUserAliases[i].Level1, Level1)) {
            if (Level2 == 0) {
                if (gUserAliases[i].Level2[0] != 0) {
                    continue;
                }
            } else {
                if (gUserAliases[i].Level2[0] == 0 ||
                    !StringEqual(gUserAliases[i].Level2, Level2)) {
                    continue;
                }
            }
            if (First) {
                gConsole.Write(" (");
                First = 0;
            } else {
                gConsole.Write(", ");
            }
            gConsole.Write(gUserAliases[i].Alias);
            gConsole.Write("*");
        }
    }
    for (i = 0; i < gAliasCount; i++) {
        if (!StringEqual(gAliases[i].Level1, Level1)) {
            continue;
        }
        if (Level2 == 0) {
            if (gAliases[i].Level2 != 0) {
                continue;
            }
        } else {
            if (gAliases[i].Level2 == 0 || !StringEqual(gAliases[i].Level2, Level2)) {
                continue;
            }
        }
        if (First) {
            gConsole.Write(" (");
            First = 0;
        } else {
            gConsole.Write(", ");
        }
        gConsole.Write(gAliases[i].Alias);
    }
    if (!First) {
        gConsole.Write(")");
    }
}

static void CommandHelp(int Argc, char **Argv) {
    int i;
    int s;

    (void)Argc;
    (void)Argv;
    gConsole.Write("commands:\n");
    for (i = 0; i < gCommandCount; i++) {
        gConsole.Write("  ");
        gConsole.Write(gCommands[i].Name);
        if (gCommands[i].SubCount > 0) {
            gConsole.Write("  ");
            gConsole.Write(gCommands[i].Help[0] ? gCommands[i].Help : "(family)");
            HelpWriteAliasesFor(gCommands[i].Name, 0);
            gConsole.Write("\n");
            for (s = 0; s < gCommands[i].SubCount; s++) {
                gConsole.Write("    ");
                gConsole.Write(gCommands[i].Subs[s].Name);
                gConsole.Write("  ");
                gConsole.Write(gCommands[i].Subs[s].Help);
                HelpWriteAliasesFor(gCommands[i].Name, gCommands[i].Subs[s].Name);
                gConsole.Write("\n");
            }
        } else {
            gConsole.Write("  ");
            gConsole.Write(gCommands[i].Help);
            HelpWriteAliasesFor(gCommands[i].Name, 0);
            gConsole.Write("\n");
        }
    }
}

/* 内置命令：清屏（仅当前焦点窗客户区；提示符由 gConsole.OnEnter 统一显示） */
static void CommandClear(int Argc, char **Argv) {
    (void)Argc;
    (void)Argv;
    gLen = 0;
    gAtLineStart = 1;
    ConsoleSbReset();
    GuiFocusClearClient();
}

/* 内置命令：回显参数 */
static void CommandEcho(int Argc, char **Argv) {
    for (int i = 1; i < Argc; i++) {
        if (i > 1) {
            gConsole.Write(" ");
        }
        gConsole.Write(Argv[i]);
    }
    gConsole.Write("\n");
}

static void RegisterBuiltins(void) {
    gConsole.Register("help", "list commands", CommandHelp);
    gConsole.RegisterAlias("help", "?");
    gConsole.Register("clear", "clear screen", CommandClear);
    gConsole.RegisterAlias("clear", "cls");
    gConsole.Register("echo", "print arguments", CommandEcho);
    gConsole.Register("alias", "alias [name[=]cmd [sub]] (user; TOYOS.DB)", CommandAlias);
    gConsole.Register("unalias", "remove user alias", CommandUnalias);
}

void ConsolePublishBuiltin(CONSOLE_OPS *Ops) {
    Ops->RegisterBuiltins = RegisterBuiltins;
}
