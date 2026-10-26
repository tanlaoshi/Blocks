/*
 * SystemProcess.c — exec / ps / kill / set priority / run user（story-s2）
 *
 * 稳态 S2 入口：
 *   run user → ProcessRunDemo（内嵌 hello + CoopDrain）→ AfterCommand 提示符
 *   exec <file> → ClassExecPath → ProcessExec
 *                 无窗：CoopDrain 同步；开窗：WaitPrompt + 异步等退
 * 下一跳：Core/Process（ProcessExec / Load / StartElf）。桌面图标 EXEC 同契约见 ClickDesktop。
 */
#include "ShellPrivate.h"
#include "Console.h"
#include "ConsolePrivate.h"
#include "Fat.h"
#include "FileSystem.h"
#include "Gui.h"
#include "Hal.h"
#include "Process.h"
#include "Scheduler.h"
#include "Syscall.h"
#include "Tasks.h"
#include "TasksPrivate.h"

/* 卷根没有这个文件时，上课程序在 Class/。路径里已有目录则不动。 */
static int PathHasDir(const char *S) {
    const char *P;

    if (!S) {
        return 1;
    }
    for (P = S; *P; P++) {
        if (*P == '/' || *P == '\\' || *P == ':') {
            return 1;
        }
    }
    return 0;
}

static int FileThere(const char *Path) {
    UINT8 Byte;
    UINTN N = 0;

    return FileSystemReadFile(Path, &Byte, 1, &N) == FAT_OK;
}

static const char *ClassExecPath(const char *Name, char *Alt, int AltMax) {
    const char *Pre = "Class/";
    int I = 0;
    int J = 0;

    if (!Name || PathHasDir(Name) || FileThere(Name)) {
        return Name;
    }
    while (Pre[I] && I + 1 < AltMax) {
        Alt[I] = Pre[I];
        I++;
    }
    while (Name[J] && I + 1 < AltMax) {
        Alt[I++] = Name[J++];
    }
    if (Name[J] != 0) {
        return Name;
    }
    Alt[I] = 0;
    if (!FileThere(Alt)) {
        return Name;
    }
    return Alt;
}

static void CommandRunuser(int Argc, char **Argv) {
    (void)Argc;
    (void)Argv;
    /* skip MMU 板无 U 页表；嵌入 hello 须进 U-mode */
    if (!HalPagingIsOn()) {
        gConsole.Write("run user: needs MMU (board skipped paging)\n");
        return;
    }
    /*
     * Hello 进 coop 缓冲；成功则 Arm done。Input：Defer 内 Flush（Hello→done）再 Pop。
     * 不做 DeferPause：Pause 把 Defer 置 0，Drain 期间 Gui/Worker PresentFlush
     * 与进桌面初期网初始化叠在一起 → NUC 二次鼠死、短按电源失效。
     */
    gConsole.Write("run user: …\n");
    if (ProcessRunDemo() != 0) {
        gConsole.Write("run user: failed (no hello blob / load / start)\n");
    } else {
        ConsoleCoopUserOutArmRunUserDone();
    }
}

static void CommandExec(int Argc, char **Argv) {
    char Alt[40];
    const char *Path;
    int WindowApp;

    if (Argc < 2) {
        gConsole.Write("usage: exec <file>\n");
        return;
    }
    /*
     * 无窗：ProcessExec 内 CoopDrain；OnEnter 的 Defer 须 Pause，否则 CoopDrain
     * 期间 Present 被吞、鼠死。
     * 开窗：与桌面图标同契约 — WaitPrompt + 异步返回；勿在此 while Live
     *（会卡死 ShellTask 于 OnEnter，Gui/Present 转不动 → 空窗+鼠死）。
     * 退完由 ExitUser → ShowPrompt；PromptAfterCommand 见 gWaitPrompt 跳过。
     */
    Path = ClassExecPath(Argv[1], Alt, (int)sizeof(Alt));
    WindowApp = ProcessAppTakesWindow(Path);
    if (WindowApp && !HalPlatformIsVirtSerialConsole()) {
        gConsole.WaitPrompt();
    }
    GuiPresentDeferPause();
    if (ProcessExec(Path) != 0) {
        GuiPresentDeferResume();
        if (WindowApp && !HalPlatformIsVirtSerialConsole()) {
            gConsole.ShowPrompt();
        }
        return;
    }
    GuiPresentDeferResume();
}

static void CommandPs(int Argc, char **Argv) {
    (void)Argc;
    (void)Argv;
    for (int i = 0; i < MAX_TASKS; i++) {
        const TASK *T = SchedulerTaskByIndex(i);
        if (!T) {
            continue;
        }
        gConsole.Write("  pid=");
        gConsole.WriteHex32((UINT32)(i + 1));
        gConsole.Write(" ");
        gConsole.Write(T->Name);
        if (T->IsUser) {
            gConsole.Write(" user");
        } else {
            gConsole.Write(" kern");
        }
        if (T->State == TASK_ZOMBIE) {
            gConsole.Write(" zombie");
        } else if (T->State == TASK_BLOCKED) {
            gConsole.Write(" blocked");
        }
        gConsole.Write(" root=");
        gConsole.WriteHex64(T->PageRoot);
        gConsole.Write(" rip=");
        gConsole.WriteHex64(SchedulerTaskRip(T));
        gConsole.Write(" ticks=");
        gConsole.WriteHex32(T->Ticks);
        gConsole.Write(" cpu=");
        gConsole.WriteHex32((UINT32)T->OnCpu);
        gConsole.Write(" home=");
        gConsole.WriteHex32((UINT32)T->HomeCpu);
        gConsole.Write(" prio=");
        if (T->Priority < 0) {
            gConsole.Write("-");
            gConsole.WriteHex32((UINT32)(-T->Priority));
        } else {
            gConsole.WriteHex32((UINT32)T->Priority);
        }
        if (SchedulerCurrent() == T) {
            gConsole.Write(" *");
        }
        gConsole.Write("\n");
    }
    gConsole.Write("cpu ticks=");
    gConsole.WriteHex64(HalCpuTicks(0));
    gConsole.Write(" worker loops=");
    gConsole.WriteHex32(WorkerLoopCount());
    gConsole.Write(" steals=");
    gConsole.WriteHex64(SchedulerStealCount());
    gConsole.Write("\n");
}

static int ParseDecInt(const char *S, INT32 *Out) {
    INT32 V = 0;
    int Neg = 0;
    if (!S || !S[0] || !Out) {
        return -1;
    }
    if (*S == '-') {
        Neg = 1;
        S++;
        if (!*S) {
            return -1;
        }
    }
    for (; *S; S++) {
        if (*S < '0' || *S > '9') {
            return -1;
        }
        V = V * 10 + (*S - '0');
    }
    *Out = Neg ? -V : V;
    return 0;
}

/* PR-S-lock：set priority <pid> <prio>；pid 与 list tasks 一致 */
static void CommandSetPriority(int Argc, char **Argv) {
    INT32 Pid = 0;
    INT32 Priority = 0;

    if (Argc < 3) {
        gConsole.Write("usage: set priority <pid> <prio>\n");
        gConsole.Write("  prio: -128..127 (higher runs sooner; shell/gui default 8)\n");
        return;
    }
    if (ParseDecInt(Argv[1], &Pid) != 0 || Pid <= 0) {
        gConsole.Write("set priority: bad pid\n");
        return;
    }
    if (ParseDecInt(Argv[2], &Priority) != 0) {
        gConsole.Write("set priority: bad prio\n");
        return;
    }
    if (SchedulerSetPriority(Pid, Priority) != 0) {
        gConsole.Write("set priority: fail\n");
        return;
    }
    gConsole.Write("set priority: pid=");
    gConsole.WriteHex32((UINT32)Pid);
    gConsole.Write(" prio=");
    if (Priority < 0) {
        gConsole.Write("-");
        gConsole.WriteHex32((UINT32)(-Priority));
    } else {
        gConsole.WriteHex32((UINT32)Priority);
    }
    gConsole.Write("\n");
}

/* PR-P4：kill <pid> [sig]；pid 与 ps / fork 一致（槽位+1）；默认 SIGTERM */
static void CommandKill(int Argc, char **Argv) {
    INT32 Pid = 0;
    INT32 Sig = SIGTERM;

    if (Argc < 2) {
        gConsole.Write("usage: kill <pid> [sig]\n");
        gConsole.Write("  sig: 2=INT 9=KILL 15=TERM (default)\n");
        return;
    }
    if (ParseDecInt(Argv[1], &Pid) != 0 || Pid <= 0) {
        gConsole.Write("kill: bad pid\n");
        return;
    }
    if (Argc >= 3) {
        if (ParseDecInt(Argv[2], &Sig) != 0) {
            gConsole.Write("kill: bad sig\n");
            return;
        }
    }
    if (SchedulerKillPid(Pid, Sig) != 0) {
        gConsole.Write("kill: failed (user only; INT/KILL/TERM)\n");
        return;
    }
    gConsole.Write("kill: ok\n");
}

void ShellCommandsSystemProcRegisterVirtMin(void) {
    gConsole.Register2("list", "tasks", "list tasks", CommandPs);
    gConsole.RegisterAliasLine("ps", "list", "tasks");
    gConsole.Register2("run", "user", "run embedded hello ELF", CommandRunuser);
    gConsole.RegisterAliasLine("runuser", "run", "user");
    gConsole.Register("execute", "load ELF (BLOCKS:FILE)", CommandExec);
    gConsole.RegisterAlias("execute", "exec");
    gConsole.Register("kill", "signal user task (PR-P4)", CommandKill);
}

void ShellCommandsSystemProcRegister(void) {
    gConsole.Register2("list", "tasks", "list tasks", CommandPs);
    gConsole.RegisterAliasLine("ps", "list", "tasks");
    gConsole.RegisterAliasLine("tasks", "list", "tasks");

    gConsole.Register2("run", "user", "run embedded hello ELF", CommandRunuser);
    gConsole.RegisterAliasLine("runuser", "run", "user");

    gConsole.Register2("set", "priority", "set priority <pid> <prio>", CommandSetPriority);
    gConsole.RegisterAliasLine("nice", "set", "priority");

    gConsole.Register("execute", "load ELF (BLOCKS:FILE / A:FILE)", CommandExec);
    gConsole.RegisterAlias("execute", "exec");
    gConsole.Register("kill", "signal user task (PR-P4)", CommandKill);
}
