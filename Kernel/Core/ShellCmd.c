/*
 * ShellCmd.c — 命令表（K22 内置；K23 ls/cat）
 *
 * 【初学者】
 * 空白分词成 Argc/Argv，再按名查表。无管道、无引号转义。
 */
#include "ShellCmd.h"
#include "FatFile.h"
#include "HalSerial.h"
#include "Process.h"

#define ARG_MAX   8
#define NAME_MAX  16
#define HELP_MAX  40
#define CMD_MAX   16

typedef void (*SHELL_CMD_FN)(int Argc, char **Argv);

typedef struct {
    char Name[NAME_MAX];
    char Help[HELP_MAX];
    SHELL_CMD_FN Fn;
} SHELL_CMD;

static SHELL_CMD gCmds[CMD_MAX];
static int gCmdN;
static char gArgBuf[120];
static char *gArgv[ARG_MAX];

static void Put(const char *S) {
    HalSerialWriteShell(S);
}

static int StrEq(const char *A, const char *B) {
    if (A == 0 || B == 0) {
        return 0;
    }
    while (*A && *B && *A == *B) {
        A++;
        B++;
    }
    return *A == 0 && *B == 0;
}

static void StrCopyCap(char *Dst, const char *Src, int Cap) {
    int i;
    if (Dst == 0 || Cap <= 0) {
        return;
    }
    if (Src == 0) {
        Dst[0] = 0;
        return;
    }
    for (i = 0; i + 1 < Cap && Src[i]; i++) {
        Dst[i] = Src[i];
    }
    Dst[i] = 0;
}

static int Register(const char *Name, const char *Help, SHELL_CMD_FN Fn) {
    if (gCmdN >= CMD_MAX || Name == 0 || Fn == 0) {
        return -1;
    }
    StrCopyCap(gCmds[gCmdN].Name, Name, NAME_MAX);
    StrCopyCap(gCmds[gCmdN].Help, Help ? Help : "", HELP_MAX);
    gCmds[gCmdN].Fn = Fn;
    gCmdN++;
    return 0;
}

/* 就地分词：Line 拷进 gArgBuf */
static int SplitArgs(const char *Line) {
    int N = 0;
    int i = 0;
    int In = 0;

    if (Line == 0) {
        return 0;
    }
    while (Line[i] && i + 1 < (int)sizeof(gArgBuf)) {
        gArgBuf[i] = Line[i];
        i++;
    }
    gArgBuf[i] = 0;

    i = 0;
    while (gArgBuf[i]) {
        while (gArgBuf[i] == ' ' || gArgBuf[i] == '\t') {
            gArgBuf[i++] = 0;
        }
        if (gArgBuf[i] == 0) {
            break;
        }
        if (N >= ARG_MAX) {
            break;
        }
        gArgv[N++] = &gArgBuf[i];
        In = 1;
        while (gArgBuf[i] && gArgBuf[i] != ' ' && gArgBuf[i] != '\t') {
            i++;
        }
        if (gArgBuf[i] == 0) {
            break;
        }
        gArgBuf[i++] = 0;
        (void)In;
    }
    return N;
}

static void CmdHelp(int Argc, char **Argv) {
    int i;
    (void)Argc;
    (void)Argv;
    Put("commands:\n");
    for (i = 0; i < gCmdN; i++) {
        Put("  ");
        Put(gCmds[i].Name);
        Put("  ");
        Put(gCmds[i].Help);
        Put("\n");
    }
}

static void CmdClear(int Argc, char **Argv) {
    int i;
    (void)Argc;
    (void)Argv;
    /* 无 GUI 客户区清屏时：多打空行冲掉串口可视区 */
    for (i = 0; i < 24; i++) {
        Put("\n");
    }
}

static void CmdEcho(int Argc, char **Argv) {
    int i;
    for (i = 1; i < Argc; i++) {
        if (i > 1) {
            Put(" ");
        }
        Put(Argv[i]);
    }
    Put("\n");
}


static void PutHex32(UINT32 V) {
    char Hex[12];
    HalSerialFormatHex(Hex, V, 8);
    Put(Hex);
}

static void CmdLs(int Argc, char **Argv) {
    FAT_DIR_ENT Ents[64];
    UINT32 N = 0;
    UINT32 i;
    (void)Argc;
    (void)Argv;
    if (FatDirListRoot(Ents, 64, &N) != 0) {
        Put("ls: fail\n");
        return;
    }
    for (i = 0; i < N; i++) {
        Put(Ents[i].IsDir ? "d " : "- ");
        PutHex32(Ents[i].Size);
        Put("  ");
        Put(Ents[i].Name);
        Put("\n");
    }
}

static void CmdCat(int Argc, char **Argv) {
    UINT8 Buf[2048];
    UINT32 Size = 0;
    int N;
    UINT32 i;
    if (Argc < 2) {
        Put("cat: need file\n");
        return;
    }
    N = FatFileReadPath(Argv[1], Buf, sizeof(Buf) - 1u, &Size);
    if (N < 0) {
        Put("cat: fail\n");
        return;
    }
    for (i = 0; i < Size; i++) {
        char One[2];
        UINT8 C = Buf[i];
        if (C == 0) {
            break;
        }
        if (C == '\n' || C == '\r' || (C >= 0x20u && C <= 0x7eu)) {
            One[0] = (char)C;
            One[1] = 0;
            Put(One);
        } else {
            Put(".");
        }
    }
    if (Size == 0 || Buf[Size - 1] != '\n') {
        Put("\n");
    }
}

static void CmdHello(int Argc, char **Argv) {
    (void)Argc;
    (void)Argv;
    (void)ProcessRunHello();
}

void ShellCmdInitialize(void) {
    gCmdN = 0;
    Register("help", "list commands", CmdHelp);
    Register("clear", "clear screen", CmdClear);
    Register("echo", "print arguments", CmdEcho);
    Register("hello", "run HELLO.ELF", CmdHello);
    Register("ls", "list root dir", CmdLs);
    Register("cat", "print text file", CmdCat);
    Put("Shell: cmds ok\n");
}

void ShellCmdRunLine(const char *Line) {
    int Argc;
    int i;

    Argc = SplitArgs(Line);
    if (Argc <= 0) {
        return;
    }
    for (i = 0; i < gCmdN; i++) {
        if (StrEq(gCmds[i].Name, gArgv[0])) {
            gCmds[i].Fn(Argc, gArgv);
            return;
        }
    }
    Put("unknown: ");
    Put(gArgv[0]);
    Put(" (try help)\n");
}
