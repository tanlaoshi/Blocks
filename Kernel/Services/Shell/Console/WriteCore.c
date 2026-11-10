/*
 * WriteCore.c — Write / WriteLength（自 Write.c 纯搬 · PR-NORM-16）
 */
#include "Console.h"
#include "ConsolePrivate.h"
#include "UI.h"
#include "Hal.h"
#include "Gui.h"
#include "Font.h"
#include "SettingsUi.h"
#include "Locale.h"
#include "HIDKeyboard.h"
#include "LibWrite.h"
#include "ShellCommands.h"
#include "Scheduler.h"
#include "SerialLog.h"
#include "HalSerial.h"

extern int gCoopDrain;

/* 同时输出到串口与屏幕（帧缓冲未就绪时只写串口） */
void ConsoleWriteText(const char *Text) {
    const char *P;
    int Owner;
    TASK *T;

    if (Text == 0) {
        return;
    }
    ConsoleOutLock();
    /*
     * 开过 USER 窗的进程：stdout 当运行时日志，勿占 Shell/COM1/blocks>。
     * ready（ShellOwn）后 → ring + RUNTIME.LOG（与 Log* 同路）；此前罕见仍 WriteShell。
     * Hello 等无窗进程仍走下方 Shell 路径。
     * LogMisc 勿改 gAtLineStart：否则「closed ok\\n」让下一行 Prompt 以为已在行首，
     * 与已有 blocks> 粘成「blocks> blocks>」。
     */
    T = SchedulerCurrent();
    if (T && T->IsUser && T->StdoutSerialOnly) {
        if (HalSerialShellOwned()) {
            LogMisc(Text);
        } else {
            for (P = Text; *P; P++) {
                gAtLineStart = (*P == '\n');
            }
            HalConsoleWriteSerial(Text);
        }
        ConsoleOutUnlock();
        return;
    }
    /*
     * Hello 等控制台 ELF（非 StdoutSerialOnly）：
     * CoopDrain 内禁止 ConsoleDrawString→PresentFlush（真机打一字卡死）；
     * 只进缓冲，DeferPop 后 CoopUserOutFlush→宿主 Write 打一次。
     * （曾 drain 内串口+Flush 再 Write →「Hello Ring3!」双行回归。）
     */
    if (T && T->IsUser) {
        int Owner;

        if (gCoopDrain) {
            ConsoleCoopUserOutAppend(Text);
            ConsoleOutUnlock();
            return;
        }
        for (P = Text; *P; P++) {
            gAtLineStart = (*P == '\n');
        }
        HalConsoleWriteSerial(Text);
        Owner = ConsoleCmdOutOwner();
        if (Owner >= 0) {
            ConsoleSbWinFeed(Owner, Text);
        } else {
            ConsoleSbFeedLiveIfBound(Text);
        }
        ConsoleOutUnlock();
        return;
    }
    for (P = Text; *P; P++) {
        gAtLineStart = (*P == '\n');
    }
    if (!HalConsoleVideoReady()) {
        HalConsoleWriteSerial(Text);
        ConsoleOutUnlock();
        return;
    }
    /*
     * 命令执行中：输出钉在发起 Shell，点到另一窗也不串台。
     * （SMP 上 Gui 可与 Shell 并行，焦点会中途变。）
     */
    Owner = ConsoleCmdOutOwner();
    if (Owner >= 0) {
        int Cur = GuiFocusIndex();

        HalConsoleWriteSerial(Text);
        if (Cur == Owner && GuiFocusKind() == GUI_WIN_SHELL &&
            GuiShellWindowActive(Owner)) {
            ConsoleSbBindFocus();
            ConsoleSbEnsureLive();
            ConsoleSbFeed(Text);
            if (GuiShellAcceptsInput()) {
                ConsoleDrawString(Text, ThemeShellText());
            }
            ConsoleSbBarAfterWrite();
        } else {
            ConsoleSbWinFeed(Owner, Text);
        }
        ConsoleOutUnlock();
        return;
    }
    /*
     * 仅 Shell 客户区接受控制台绘制。焦点在 USER/Settings/Files 时若仍画 FB，
     * 会与 USER 窗标签等叠字，并污染用户窗备份（透视/花屏）。
     */
    if (GuiFocusKind() != GUI_WIN_SHELL) {
        /*
         * 用户窗（Snake/TaskManager…）焦点在 USER：stdout 不进教学串口、也不 Feed Shell。
         * 旧逻辑「只走串口」会把 snake: 打进宿主 blocks>，看起来像占住终端。
         */
        if (GuiFocusKind() == GUI_WIN_USER) {
            ConsoleOutUnlock();
            return;
        }
        HalConsoleWriteSerial(Text);
        {
            int i;
            int HasShell;

            /*
             * 尚无 Shell 时勿记（Arm64 自测 write 会污染开窗）。
             * 有 Shell：只写入已 Bind 的 live 窗，绝不在未聚焦时乱 Bind。
             */
            HasShell = 0;
            for (i = 0; i < GUI_MAX_WINS; i++) {
                if (GuiShellWindowActive(i)) {
                    HasShell = 1;
                    break;
                }
            }
            if (HasShell) {
                ConsoleSbFeedLiveIfBound(Text);
            }
        }
        ConsoleOutUnlock();
        return;
    }
    HalConsoleWriteSerial(Text);
    ConsoleSbBindFocus();
    ConsoleSbEnsureLive();
    ConsoleSbFeed(Text);
    ConsoleDrawString(Text, ThemeShellText());
    ConsoleSbBarAfterWrite();
    ConsoleOutUnlock();
}

/* 按长度输出（SYS_FILE_WRITE 用；UTF-8 按码点画，不因中间的 NUL 截断） */
void ConsoleWriteLength(const char *Data, UINTN Len) {
    UINTN i;

    if (Data == 0 || Len == 0) {
        return;
    }
    /*
     * 整段持锁：逐码点 Write 时勿让 Prompt 插队（见 Console.c Prompt）。
     * Write 自身也加锁 → 此处不锁，改由外层包住会嵌套死锁；故 WriteLength
     * 直接调 gConsole.Write 前依赖 Write 内锁。逐字间隙仍会被 Prompt 插入。
     * 用一次「合并写」：先拼到栈缓冲再单次 Write。
     */
    if (Len < 256) {
        char Tmp[256];
        UINTN n;

        for (n = 0; n < Len; n++) {
            Tmp[n] = Data[n];
        }
        Tmp[Len] = 0;
        /* 含中间 NUL 时仍走下方逐码点（罕见） */
        for (n = 0; n < Len; n++) {
            if (Tmp[n] == 0 && n + 1 < Len) {
                break;
            }
        }
        if (n >= Len) {
            gConsole.Write(Tmp);
            return;
        }
    }
    i = 0;
    while (i < Len) {
        UINT32 Cp;
        UINTN N;
        char Tmp[5];
        UINTN k;

        if (Data[i] == '\n') {
            gConsole.Write("\n");
            i++;
            continue;
        }
        if (Data[i] == '\0') {
            i++;
            continue;
        }
        /* 非法/截断 UTF-8：跳过单字节，避免死循环 */
        if (i + 4 > Len) {
            /* 剩余可能不足完整序列；尽量解，失败则跳过 */
        }
        N = Utf8Decode(Data + i, &Cp);
        if (N == 0 || i + N > Len) {
            i++;
            continue;
        }
        if (Cp < 32 || Cp == 127) {
            i += N;
            continue;
        }
        for (k = 0; k < N && k < sizeof(Tmp) - 1; k++) {
            Tmp[k] = Data[i + k];
        }
        Tmp[k] = 0;
        gConsole.Write(Tmp);
        i += N;
    }
}

