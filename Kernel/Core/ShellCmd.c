/*
 * ShellCmd.c — K22 命令表（help / clear / echo / hello）
 *
 * 【初学者】
 * 空白分词成 Argc/Argv，再按名查表。无管道、无引号转义。
 */
#include "ShellCmd.h"
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
