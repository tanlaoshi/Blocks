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
#include "SpinLock.h"
#include "Locale.h"
#include "KernelModulesPrivate.h"
#include "ShellCommands.h"
#include "Store.h"
#include "SerialLog.h"

/* Stdin/Job → ConsoleStdin.c / ConsoleJob.c（PR-NORM-16）。 */

char gLine[LINE_MAX];
int gLen;
int gWaitPrompt;
int gPromptSuspend;
int gAtLineStart = 1;
static SPIN_LOCK gConsoleOutLock;


void ConsoleOutLock(void) {
    SpinLockAcquire(&gConsoleOutLock);
}

void ConsoleOutUnlock(void) {
    SpinLockRelease(&gConsoleOutLock);
}

static void PromptCommon(int ToSerial) {
    /*
     * 整段在 OutLock 内：SMP 上勿把 \\n 与 Blocks> 拆开，否则用户逐字 Write
     * 会插成「H\\nello」+「Blocks>」粘在一起。
     * ToSerial=0：开 Shell 窗只画客户区；串口已由 AnnounceBootReady 打过一份。
     */
    ConsoleOutLock();
    gLen = 0;
    /* 仅当不在行首时换行：开窗欢迎语后已有 \\n，再打会空一行像「提示语不对」 */
    if (!gAtLineStart) {
        if (ToSerial) {
            HalConsoleWriteSerial("\n");
        }
        if (HalConsoleVideoReady() && GuiFocusKind() == GUI_WIN_SHELL) {
            ConsoleSbBindFocus();
            ConsoleSbEnsureLive();
            ConsoleSbFeed("\n");
            ConsoleDrawString("\n", ThemeShellText());
        }
    }
    if (ToSerial) {
        HalConsoleWriteSerial("Blocks> ");
    }
    gAtLineStart = 0;
    if (HalConsoleVideoReady() && GuiFocusKind() == GUI_WIN_SHELL) {
        ConsoleSbBindFocus();
        ConsoleSbEnsureLive();
        ConsoleSbFeed("Blocks> ");
        ConsoleDrawString("Blocks> ", ThemeShellPrompt());
    }
    ConsoleOutUnlock();
}

void Prompt(void) {
    PromptCommon(1);
}

void PromptScreen(void) {
    PromptCommon(0);
}

static void WaitPrompt(void) {
    /*
     * 桌面 EXEC 常在「blocks> 」上占 stdin；先离开提示符行一次。
     * 勿在 Write 里对用户逐字补 \\n（printf 每字节一次 → 一字一行）。
     * shell OnEnter 已换行时 gAtLineStart=1，此处跳过。
     */
    if (!gAtLineStart) {
        gConsole.Write("\n");
    }
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
