/*
 * ShellCommandsFs.c — PR-S3-shellfs-1：ls/cat/write/rm/mkdir/rmdir/mv + 注册汇总
 *
 * wrbig/vols/filestat/… 见 ShellCommandsFsExtra.c；install 见 ShellCommandsInstall.c。
 */
#include "ShellCommands.h"
#include "ShellPrivate.h"
#include "FileSystem.h"
#include "Fat.h"
#include "Console.h"

void ShellFatReport(const char *Cmd, int Err) {
    gConsole.Write(Cmd);
    gConsole.Write(": ");
    gConsole.Write(FatStrError(Err));
    gConsole.Write("\n");
}

static void CommandLs(int Argc, char **Argv) {
    const char *Path = (Argc >= 2) ? Argv[1] : 0;
    int Err = FileSystemListDirectory(Path);
    if (Err != FAT_OK) {
        ShellFatReport("ls", Err);
    }
}

static void CommandCat(int Argc, char **Argv) {
    static UINT8 Buf[4096];
    UINTN Size = 0;
    int Err;

    if (Argc < 2) {
        gConsole.Write("usage: cat <file>\n");
        return;
    }
    Err = FileSystemReadFile(Argv[1], Buf, sizeof(Buf) - 1, &Size);
    if (Err != FAT_OK) {
        ShellFatReport("cat", Err);
        return;
    }
    Buf[Size] = 0;
    gConsole.Write((const char *)Buf);
    if (Size > 0 && Buf[Size - 1] != '\n') {
        gConsole.Write("\n");
    }
}

static void CommandWrite(int Argc, char **Argv) {
    static char Buf[512];
    UINTN Len = 0;
    int a;
    int first = 1;
    int Err;

    if (Argc < 3) {
        gConsole.Write("usage: write <file> <text...>\n");
        return;
    }
    for (a = 2; a < Argc; a++) {
        const char *S = Argv[a];
        if (!first) {
            if (Len + 1 >= sizeof(Buf)) {
                break;
            }
            Buf[Len++] = ' ';
        }
        first = 0;
        while (*S && Len + 1 < sizeof(Buf)) {
            Buf[Len++] = *S++;
        }
    }
    if (Len + 1 < sizeof(Buf)) {
        Buf[Len++] = '\n';
    }
    Buf[Len] = 0;
    Err = FileSystemWriteFile(Argv[1], Buf, Len);
    if (Err != FAT_OK) {
        ShellFatReport("write", Err);
        return;
    }
    gConsole.Write("write: ok ");
    gConsole.WriteHex32((UINT32)Len);
    gConsole.Write(" bytes\n");
}

static void CommandRm(int Argc, char **Argv) {
    int Err;

    if (Argc < 2) {
        gConsole.Write("usage: rm <file>\n");
        return;
    }
    Err = FileSystemDeleteFile(Argv[1]);
    if (Err != FAT_OK) {
        ShellFatReport("rm", Err);
        return;
    }
    gConsole.Write("rm: ok\n");
}

static void CommandMkdir(int Argc, char **Argv) {
    int Err;

    if (Argc < 2) {
        gConsole.Write("usage: mkdir <dir>\n");
        return;
    }
    /* PR-S-bundle-fs：mkdir 走 MakePath，支持 Apps/id/Assets 多级 */
    Err = FileSystemMakePath(Argv[1]);
    if (Err != FAT_OK) {
        ShellFatReport("mkdir", Err);
        return;
    }
    gConsole.Write("mkdir: ok\n");
}

static void CommandRmdir(int Argc, char **Argv) {
    int Err;

    if (Argc < 2) {
        gConsole.Write("usage: rmdir <dir> | rmdir -r <dir>\n");
        return;
    }
    if (Argc >= 3 && Argv[1][0] == '-' && Argv[1][1] == 'r' && Argv[1][2] == 0) {
        Err = FileSystemRemoveTree(Argv[2]);
    } else {
        Err = FileSystemRemoveDirectory(Argv[1]);
    }
    if (Err != FAT_OK) {
        ShellFatReport("rmdir", Err);
        return;
    }
    gConsole.Write("rmdir: ok\n");
}

static void CommandMv(int Argc, char **Argv) {
    int Err;

    if (Argc < 3) {
        gConsole.Write("usage: mv <old> <new>\n");
        return;
    }
    Err = FileSystemRename(Argv[1], Argv[2]);
    if (Err != FAT_OK) {
        ShellFatReport("mv", Err);
        return;
    }
    gConsole.Write("mv: ok\n");
}

void ShellCommandsRegisterFs(void) {
    /* list：默认列目录；二级 tasks/devices 已由 ShellCommandsRegister 挂上 */
    gConsole.Register("list", "list directory (BLOCKS: / A: / RES:)", CommandLs);
    gConsole.RegisterAlias("list", "ls");
    gConsole.RegisterAlias("list", "dir");

    gConsole.Register("print", "print file", CommandCat);
    gConsole.RegisterAlias("print", "cat");
    gConsole.RegisterAlias("print", "type");

    gConsole.Register("write", "write file text", CommandWrite);

    gConsole.Register2("make", "directory", "create directory", CommandMkdir);
    gConsole.RegisterAliasLine("mkdir", "make", "directory");
    gConsole.RegisterAliasLine("md", "make", "directory");

    gConsole.Register("remove", "remove file or empty dir", CommandRm);
    gConsole.Register2("remove", "directory", "remove empty directory", CommandRmdir);
    gConsole.RegisterAlias("remove", "rm");
    gConsole.RegisterAlias("remove", "del");
    gConsole.RegisterAliasLine("rmdir", "remove", "directory");
    gConsole.RegisterAliasLine("rd", "remove", "directory");

    gConsole.Register("move", "rename/move file or dir", CommandMv);
    gConsole.RegisterAlias("move", "mv");
    gConsole.RegisterAlias("move", "rename");

    ShellCommandsFsExtraRegister();
    ShellCommandsInstallRegister();
}
