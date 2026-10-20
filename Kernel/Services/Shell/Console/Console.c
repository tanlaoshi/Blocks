/*
 * Console.c — Shell 提示符与 Job 输出（PR-S3-console-2）
 *
 * 模块表 ConsoleInitialize（软成功：恒 return 0）：
 *   PublishAll → StorePathGuard → Locale → Builtins/Commands
 *   → UserAliasLoad → gConsole.Initialize
 * ModulesRun 进表前已 PublishAll 一次（Db 早登记）；此处再填。
 * 焦点 / 开窗重画见 Focus.c。
 */
#include "Console.h"
#include "ConsolePrivate.h"
#include "UI.h"
#include "Hal.h"
#include "Gui.h"
#include "Font.h"
#include "Scheduler.h"
#include "Locale.h"
#include "KernelModulesPrivate.h"
#include "ShellCommands.h"
#include "Store.h"

char gLine[LINE_MAX];
int gLen;
int gWaitPrompt;
int gPromptSuspend;
int gAtLineStart = 1;
/* -2=无 Job；-1=串口壳；>=0=发起 Shell 窗下标 */
static int gJobPromptWin = -2;
/* Worker 在 RunLine 内已 Release 并打过 blocks> 时，阻止 PromptAfterCommand 再打一次 */
static int gJobSkipAfterCommand;
/* -2=非命令输出；-1=串口；>=0=本条命令输出归属窗（跟发起窗，不跟焦点） */
static int gCmdOutWin = -2;
static int gBootReadyAnnounced;

/* 用户 ELF read(0)：Shell 泄串口/键盘入环；须 ≥ 真机单轮 MaxRx，防粘贴丢字 */
#define STDIN_Q_CAP 256
static char gStdinQ[STDIN_Q_CAP];
static int gStdinR;
static int gStdinW;
static int gStdinN;

static int StdinUserHold(void) {
    return gWaitPrompt > 0 || SchedulerLiveUserApps();
}

static void StdinFlush(void) {
    gStdinR = 0;
    gStdinW = 0;
    gStdinN = 0;
}

static void StdinPut(char C) {
    if (gStdinN >= STDIN_Q_CAP) {
        return;
    }
    gStdinQ[gStdinW] = C;
    gStdinW++;
    if (gStdinW >= STDIN_Q_CAP) {
        gStdinW = 0;
    }
    gStdinN++;
}

static char StdinGetChar(void) {
    for (;;) {
        if (gStdinN > 0) {
            char C = gStdinQ[gStdinR];
            gStdinR++;
            if (gStdinR >= STDIN_Q_CAP) {
                gStdinR = 0;
            }
            gStdinN--;
            return C;
        }
        if (HalSerialDataReady()) {
            return HalSerialReadChar();
        }
        HalCpuHalt();
    }
}

static void AnnounceBootReady(void) {
    if (gBootReadyAnnounced) {
        return;
    }
    gBootReadyAnnounced = 1;
    HalConsoleWriteSerial(LocStr(MSG_CON_READY));
    HalConsoleWriteSerial("\n");
    HalConsoleWriteSerial("hint: type commands in THIS terminal (not QEMU window)\n");
    Prompt();
    HalSerialShellOwn();
}

void Prompt(void) {
    gLen = 0;
    /* 仅当不在行首时换行：开窗欢迎语后已有 \\n，再打会空一行像「提示语不对」 */
    if (!gAtLineStart) {
        gConsole.Write("\n");
    }
    HalConsoleWriteSerial("blocks> ");
    gAtLineStart = 0;
    if (HalConsoleVideoReady() && GuiFocusKind() == GUI_WIN_SHELL) {
        ConsoleSbBindFocus();
        ConsoleSbEnsureLive();
        ConsoleSbFeed("blocks> ");
        ConsoleDrawString("blocks> ", ThemeShellPrompt());
    }
}

static void WaitPrompt(void) {
    gWaitPrompt++;
}

static void ShowPrompt(void) {
    if (gWaitPrompt > 0) {
        gWaitPrompt--;
    }
    if (gWaitPrompt == 0) {
        gConsole.StdinFlush();
        Prompt();
    }
}

static void Notify(const char *Text) {
    if (!gConsole.PromptSuspended()) {
        gConsole.Write("\n");
    }
    if (Text != 0) {
        gConsole.Write(Text);
    }
}

static void SuspendPrompt(void) {
    gPromptSuspend++;
}

static void ResumePrompt(void) {
    if (gPromptSuspend > 0) {
        gPromptSuspend--;
    }
    if (gPromptSuspend == 0 && gWaitPrompt == 0) {
        Prompt();
    }
}

static int PromptSuspended(void) {
    return gPromptSuspend > 0;
}

static void JobHoldPrompt(void) {
    if (HalConsoleOnly()) {
        gJobPromptWin = -1;
    } else if (GuiFocusKind() == GUI_WIN_SHELL) {
        gJobPromptWin = GuiFocusIndex();
    } else {
        gJobPromptWin = -1;
    }
    gJobSkipAfterCommand = 0;
    gConsole.WaitPrompt();
}

static int JobConsumeSkipAfterCommand(void) {
    if (!gJobSkipAfterCommand) {
        return 0;
    }
    gJobSkipAfterCommand = 0;
    return 1;
}

static int JobPromptPending(void) {
    return (gJobPromptWin != -2) ? 1 : 0;
}

static void JobShiftRaise(int Idx, int Top) {
    if (gJobPromptWin == Idx) {
        gJobPromptWin = Top;
    } else if (gJobPromptWin > Idx && gJobPromptWin <= Top) {
        gJobPromptWin--;
    }
    if (gCmdOutWin == Idx) {
        gCmdOutWin = Top;
    } else if (gCmdOutWin > Idx && gCmdOutWin <= Top) {
        gCmdOutWin--;
    }
}

void ConsoleCmdOutBegin(void) {
    if (HalConsoleOnly()) {
        gCmdOutWin = -1;
    } else if (GuiFocusKind() == GUI_WIN_SHELL) {
        gCmdOutWin = GuiFocusIndex();
    } else {
        gCmdOutWin = -1;
    }
}

void ConsoleCmdOutEnd(void) {
    gCmdOutWin = -2;
}

int ConsoleCmdOutOwner(void) {
    return gCmdOutWin;
}

static void WriteToJobShell(const char *Text) {
    int Owner = gJobPromptWin;
    int Cur;

    if (!Text) {
        return;
    }
    HalConsoleWriteSerial(Text);
    if (Owner < 0 || HalConsoleOnly()) {
        if (HalConsoleVideoReady() && GuiFocusKind() == GUI_WIN_SHELL) {
            ConsoleSbEnsureLive();
            ConsoleSbFeed(Text);
            ConsoleDrawString(Text, ThemeShellText());
        }
        return;
    }
    Cur = GuiFocusIndex();
    if (Cur == Owner && GuiShellWindowActive(Owner)) {
        if (HalConsoleVideoReady()) {
            ConsoleSbBindFocus();
            ConsoleSbEnsureLive();
            ConsoleSbFeed(Text);
            if (GuiShellAcceptsInput()) {
                ConsoleDrawString(Text, ThemeShellText());
            }
        } else {
            ConsoleSbWinFeed(Owner, Text);
        }
        return;
    }
    ConsoleSbWinFeed(Owner, Text);
}

static void JobReleasePrompt(void) {
    int Owner = gJobPromptWin;
    int Cur;

    if (Owner == -2) {
        return;
    }
    gJobPromptWin = -2;
    if (HalConsoleOnly() || Owner < 0) {
        gConsole.ShowPrompt();
        gJobSkipAfterCommand = 1;
        return;
    }
    if (!GuiShellWindowActive(Owner)) {
        if (gWaitPrompt > 0) {
            gWaitPrompt--;
        }
        gJobSkipAfterCommand = 1;
        return;
    }
    Cur = GuiFocusIndex();
    if (Cur == Owner) {
        gConsole.ShowPrompt();
        GuiConsolePush(gLine, gLen, gWaitPrompt);
        gJobSkipAfterCommand = 1;
        return;
    }
    /* 已切走：完成行在 WinFeed；槽内补提示符并清该窗 WaitPrompt */
    ConsoleSbWinFeed(Owner, "blocks> ");
    GuiConsoleSetWaitPrompt(Owner, 0);
    if (gWaitPrompt > 0) {
        gWaitPrompt--;
    }
    gJobSkipAfterCommand = 1;
}

CONSOLE_OPS gConsole;

void ConsolePublishShell(CONSOLE_OPS *Ops) {
    Ops->WriteToJobShell = WriteToJobShell;
    Ops->WaitPrompt = WaitPrompt;
    Ops->ShowPrompt = ShowPrompt;
    Ops->StdinUserHold = StdinUserHold;
    Ops->StdinPut = StdinPut;
    Ops->StdinGetChar = StdinGetChar;
    Ops->StdinFlush = StdinFlush;
    Ops->SuspendPrompt = SuspendPrompt;
    Ops->ResumePrompt = ResumePrompt;
    Ops->PromptSuspended = PromptSuspended;
    Ops->Notify = Notify;
    Ops->JobHoldPrompt = JobHoldPrompt;
    Ops->JobReleasePrompt = JobReleasePrompt;
    Ops->JobPromptPending = JobPromptPending;
    Ops->JobConsumeSkipAfterCommand = JobConsumeSkipAfterCommand;
    Ops->JobShiftRaise = JobShiftRaise;
    Ops->AnnounceBootReady = AnnounceBootReady;
}

void ConsolePublishAll(void) {
    ConsolePublishWrite(&gConsole);
    ConsolePublishInput(&gConsole);
    ConsolePublishHistory(&gConsole);
    ConsolePublishSerial(&gConsole);
    ConsolePublishCommand(&gConsole);
    ConsolePublishBuiltin(&gConsole);
    ConsolePublishAlias(&gConsole);
    ConsolePublishFocus(&gConsole);
    ConsolePublishShell(&gConsole);
    ConsolePublishScroll(&gConsole);
    ConsolePublishScrollbar(&gConsole);
    ConsolePublishScrollback(&gConsole);
    ConsolePublishLog(&gConsole);
}

int ConsoleInitialize(void) {
    ConsolePublishAll();
    StoreRegisterPathGuard();
    LocaleInitialize();
    gConsole.RegisterBuiltins();
    if (HalConsoleOnly()) {
        ShellCommandsRegisterVirtMin();
    } else {
        ShellCommandsRegister();
    }
    gConsole.UserAliasLoad();
    gConsole.Initialize();
    return 0;
}
