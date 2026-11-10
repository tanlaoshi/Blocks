/*
 * AliasCommand.c — CommandAlias（自 Alias.c 纯搬 · PR-NORM-16）
 */
#include "Console.h"
#include "ConsolePrivate.h"
#include "ShellCommands.h"

/* alias / unalias（PR-C3 用户自定义；* 在 help 中标用户别名） */
void CommandAlias(int Argc, char **Argv) {
    char Name[USER_ALIAS_NAME];
    char L1[USER_ALIAS_WORD];
    char L2[USER_ALIAS_WORD];
    const char *P;
    int i;
    int j;

    if (Argc < 2) {
        if (gUserAliasCount == 0) {
            gConsole.Write("alias: (none)  usage: alias <name> <cmd> [sub]\n");
            gConsole.Write("  or alias name=cmd [sub]; unalias <name>\n");
            return;
        }
        for (i = 0; i < gUserAliasCount; i++) {
            gConsole.Write("  ");
            gConsole.Write(gUserAliases[i].Alias);
            gConsole.Write("=");
            gConsole.Write(gUserAliases[i].Level1);
            if (gUserAliases[i].Level2[0]) {
                gConsole.Write(" ");
                gConsole.Write(gUserAliases[i].Level2);
            }
            gConsole.Write("\n");
        }
        return;
    }

    /* alias name=l1 [l2] */
    P = Argv[1];
    i = 0;
    while (P[i] && P[i] != '=' && i < USER_ALIAS_NAME - 1) {
        Name[i] = P[i];
        i++;
    }
    Name[i] = 0;
    if (P[i] == '=') {
        P = P + i + 1;
        j = 0;
        while (P[j] && P[j] != ' ' && j < USER_ALIAS_WORD - 1) {
            L1[j] = P[j];
            j++;
        }
        L1[j] = 0;
        L2[0] = 0;
        if (P[j] == ' ') {
            P = P + j + 1;
            j = 0;
            while (P[j] && j < USER_ALIAS_WORD - 1) {
                L2[j] = P[j];
                j++;
            }
            L2[j] = 0;
        } else if (Argc >= 3) {
            CopyWord(L2, USER_ALIAS_WORD, Argv[2]);
        }
        if (L1[0] == 0) {
            gConsole.Write("usage: alias name=cmd [sub]\n");
            return;
        }
        if (UserAliasSet(Name, L1, L2[0] ? L2 : 0) == 0) {
            gConsole.Write("alias: set ");
            gConsole.Write(Name);
            gConsole.Write("\n");
        }
        return;
    }

    /* alias name l1 [l2] */
    if (Argc < 3) {
        gConsole.Write("usage: alias <name> <cmd> [sub]\n");
        return;
    }
    CopyWord(Name, USER_ALIAS_NAME, Argv[1]);
    CopyWord(L1, USER_ALIAS_WORD, Argv[2]);
    L2[0] = 0;
    if (Argc >= 4) {
        CopyWord(L2, USER_ALIAS_WORD, Argv[3]);
    }
    if (UserAliasSet(Name, L1, L2[0] ? L2 : 0) == 0) {
        gConsole.Write("alias: set ");
        gConsole.Write(Name);
        gConsole.Write("\n");
    }
}
