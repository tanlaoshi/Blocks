/*
 * ShellCommand.c — 命令表：注册 / 分词 / 分发
 *
 * 【初学者】
 * - 分层：Core/Console/ShellCommand 主文件；领域命令在同目录 *Register
 * - 对外入口：ShellCommandRegister、ShellCommandInitialize、ShellCommandRunLine
 * - 不做：各命令语义（见 FileSystem.c / Network.c 等）
 */
#include "ShellCommand.h"
#include "ShellSystem.h"
#include "HalSerial.h"
#include "Process.h"

#define ARG_MAX   8
#define NAME_MAX  16
#define HELP_MAX  40
#define COMMAND_MAX   40

typedef struct {
    char Name[NAME_MAX];
    char Help[HELP_MAX];
    SHELL_COMMAND_FN Fn;
} SHELL_COMMAND;

static SHELL_COMMAND gCommands[COMMAND_MAX];
static int gCommandCount;
static char gArgBuf[120];
static char *gArgv[ARG_MAX];

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

static void StringCopy(char *Destination, const char *Source, int Capacity) {
    int i;
    if (Destination == 0 || Capacity <= 0) {
        return;
    }
    if (Source == 0) {
        Destination[0] = 0;
        return;
    }
    for (i = 0; i + 1 < Capacity && Source[i]; i++) {
        Destination[i] = Source[i];
    }
    Destination[i] = 0;
}

/*
 * ShellCommandRegister — 追加一条 Shell 命令
 *
 * 谁调用：各 *Register（FileSystemRegister、NetworkRegister…）。
 * 返回：0 成功；-1 表满或参数空
 */
int ShellCommandRegister(const char *Name, const char *Help, SHELL_COMMAND_FN Fn) {
    if (gCommandCount >= COMMAND_MAX || Name == 0 || Fn == 0) {
        return -1;
    }
    StringCopy(gCommands[gCommandCount].Name, Name, NAME_MAX);
    StringCopy(gCommands[gCommandCount].Help, Help ? Help : "", HELP_MAX);
    gCommands[gCommandCount].Fn = Fn;
    gCommandCount++;
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

static void CommandHelp(int Argc, char **Argv) {
    int i;
    (void)Argc;
    (void)Argv;
    Put("commands:\n");
    for (i = 0; i < gCommandCount; i++) {
        Put("  ");
        Put(gCommands[i].Name);
        Put("  ");
        Put(gCommands[i].Help);
        Put("\n");
    }
}

static void CommandClear(int Argc, char **Argv) {
    int i;
    (void)Argc;
    (void)Argv;
    for (i = 0; i < 24; i++) {
        Put("\n");
    }
}

static void CommandEcho(int Argc, char **Argv) {
    int i;
    for (i = 1; i < Argc; i++) {
        if (i > 1) {
            Put(" ");
        }
        Put(Argv[i]);
    }
    Put("\n");
}

static void CommandHello(int Argc, char **Argv) {
    (void)Argc;
    (void)Argv;
    (void)ProcessRunHello();
}

/*
 * ShellCommandInitialize — 清零表并注册内置 + 各领域命令
 *
 * 谁调用：ConsoleInitialize。
 * 前后文：后 — ConsoleRefreshBanner
 * 返回：void
 */
void ShellCommandInitialize(void) {
    gCommandCount = 0;
    ShellCommandRegister("help", "list commands", CommandHelp);
    ShellCommandRegister("clear", "clear screen", CommandClear);
    ShellCommandRegister("echo", "print arguments", CommandEcho);
    ShellCommandRegister("hello", "run HELLO.ELF", CommandHello);
    FileSystemRegister();
    ThemeRegister();
    NetworkRegister();
    NetworkUdpRegister();
    NetworkTcpRegister();
    ShellSystemRegister();
    DataBaseRegister();
    StoreRegister();
    Put("Shell: cmds ok\n");
}

/*
 * ShellCommandRunLine — 分词并 dispatch 第一条 argv[0]
 *
 * 谁调用：ConsoleRun 每读一行。
 * 返回：void（未知命令打印 hint）
 */
void ShellCommandRunLine(const char *Line) {
    int Argc;
    int i;

    Argc = SplitArgs(Line);
    if (Argc <= 0) {
        return;
    }
    for (i = 0; i < gCommandCount; i++) {
        if (StringsEqual(gCommands[i].Name, gArgv[0])) {
            gCommands[i].Fn(Argc, gArgv);
            return;
        }
    }
    Put("unknown: ");
    Put(gArgv[0]);
    Put(" (try help)\n");
}
