/*
 * FileSystemHole.c — Shell `fs hole`：FAT 分配提示的空洞自检命令口
 *
 * 判据与打印在 Services/FileSystem/HoleTest.c；本文件只解析参数并调用。
 * 用法：fs hole [--nospc] [--rounds N] [卷前缀]
 */
#include "ShellCommands.h"
#include "ShellPrivate.h"
#include "FileSystem.h"
#include "CString.h"
#include "Console.h"

#define FS_HOLE_ROUNDS_DEFAULT 1

static int CommandParseRounds(const char *Text, int *Out) {
    int Value = 0;
    int Index = 0;

    if (Text == 0 || Text[0] == 0) {
        return 0;
    }
    while (Text[Index] != 0) {
        if (Text[Index] < '0' || Text[Index] > '9') {
            return 0;
        }
        Value = Value * 10 + (int)(Text[Index] - '0');
        if (Value > FS_HOLE_ROUNDS_MAX) {
            return 0;
        }
        Index++;
    }
    *Out = Value;
    return 1;
}

static void CommandHoleUsage(void) {
    gConsole.Write("usage: fs hole [--nospc] [--rounds 1..9] [volume prefix]\n");
    gConsole.Write("  A/B 1 MB + C 2 MB; verifies C reuses A's hole and B stays intact\n");
}

static void CommandFsHole(int Argc, char **Argv) {
    const char *Prefix = "";
    int ExpectNoSpace = 0;
    int Rounds = FS_HOLE_ROUNDS_DEFAULT;
    int Index = 1;
    int Err;

    while (Index < Argc) {
        if (StringEqual(Argv[Index], "--nospc")) {
            ExpectNoSpace = 1;
        } else if (StringEqual(Argv[Index], "--rounds")) {
            Index++;
            if (Index >= Argc || !CommandParseRounds(Argv[Index], &Rounds)) {
                CommandHoleUsage();
                return;
            }
        } else if (Argv[Index][0] == '-') {
            CommandHoleUsage();
            return;
        } else {
            Prefix = Argv[Index];
        }
        Index++;
    }
    Err = FileSystemHoleTest(Prefix, Rounds, ExpectNoSpace);
    if (Err != FAT_OK) {
        gConsole.Write("fs hole: ");
        gConsole.Write(FatStrError(Err));
        gConsole.Write("\n");
    }
}

void ShellCommandsFsHoleRegister(void) {
    gConsole.Register2("fs", "hole",
                       "A/B/C hole test: delete A, write C over A's hole",
                       CommandFsHole);
    gConsole.RegisterAliasLine("fshole", "fs", "hole");
}
