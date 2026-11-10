/*
 * ConsoleJob.c — Job 提示符 / WriteToJobShell（自 Console.c 纯搬 · PR-NORM-16）
 */
#include "Console.h"
#include "ConsolePrivate.h"
#include "UI.h"
#include "Hal.h"
#include "Gui.h"
#include "Font.h"
#include "Locale.h"
#include "SerialLog.h"

/* -2=无 Job；-1=串口壳；>=0=发起 Shell 窗下标 */
static int gJobPromptWin = -2;
/* Worker 在 RunLine 内已 Release 并打过 blocks> 时，阻止 PromptAfterCommand 再打一次 */
static int gJobSkipAfterCommand;
/* -2=非命令输出；-1=串口；>=0=本条命令输出归属窗（跟发起窗，不跟焦点） */
static int gCmdOutWin = -2;

void JobHoldPrompt(void) {
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

int JobConsumeSkipAfterCommand(void) {
    if (!gJobSkipAfterCommand) {
        return 0;
    }
    gJobSkipAfterCommand = 0;
    return 1;
}

int JobPromptPending(void) {
    return (gJobPromptWin != -2) ? 1 : 0;
}

void JobShiftRaise(int Idx, int Top) {
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

void WriteToJobShell(const char *Text) {
    int Owner = gJobPromptWin;
    int Cur;

    if (!Text) {
        return;
    }
    /*
     * Owner==-2：无 JobHold（自动 DHCP 等）。对表 c59c7f88：
     * 勿窜正打字的壳，也勿 HalConsoleWriteSerial 刷屏（Pi epoch/链路抖会多次 ok）。
     * 只 LogNet → RUNTIME.LOG；手敲 net dhcp 会 JobHold（Owner≠-2）仍上串口一行。
     */
    if (Owner == -2) {
        LogNet(Text);
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

void JobReleasePrompt(void) {
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
    ConsoleSbWinFeed(Owner, "Blocks> ");
    GuiConsoleSetWaitPrompt(Owner, 0);
    if (gWaitPrompt > 0) {
        gWaitPrompt--;
    }
    gJobSkipAfterCommand = 1;
}
