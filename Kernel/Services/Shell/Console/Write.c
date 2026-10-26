/*
 * Write.c — 串口与屏幕输出
 * 核心：Console.c
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

/*
 * CoopDrain 内用户 printf：不可 ConsoleDrawString→PresentFlush（真机卡死）。
 * 先串口+scrollback+本缓冲；Drain 回到 Shell 宿主后再 Write 刷屏。
 */
#define COOP_USER_OUT_MAX 512u
static char gCoopUserOut[COOP_USER_OUT_MAX];
static UINTN gCoopUserOutLen;

void ConsoleCoopUserOutClear(void) {
    gCoopUserOutLen = 0;
}

static void CoopUserOutAppend(const char *Text) {
    if (!Text) {
        return;
    }
    while (*Text && gCoopUserOutLen + 1u < COOP_USER_OUT_MAX) {
        gCoopUserOut[gCoopUserOutLen++] = *Text++;
    }
}

void ConsoleCoopUserOutFlush(void) {
    if (gCoopUserOutLen == 0) {
        return;
    }
    gCoopUserOut[gCoopUserOutLen] = 0;
    gCoopUserOutLen = 0;
    /* 宿主已是 Shell 内核任务：走完整 Write（串口+屏） */
    gConsole.Write(gCoopUserOut);
}

void ConsoleDrawString(const char *Text, UINT32 Color) {
    UINT32 X0;
    UINT32 Y0;
    UINT32 X1;
    UINT32 Y1;
    UINT32 L;
    UINT32 T;
    UINT32 R;
    UINT32 B;
    UINT32 Cx;
    UINT32 Cy;
    UINT32 Cw;
    UINT32 Ch;
    UINT32 Bg;

    GuiFrameBufferBegin();
    GuiFocusApplyClip();
    ConsoleSbBarReapplyClip();
    HalConsoleGetTextCursor(&X0, &Y0);
    FontTtfMonoBegin();
    HalConsoleDrawString(Text, Color);
    FontTtfMonoEnd();
    HalConsoleGetTextCursor(&X1, &Y1);
    /*
     * 换行/滚屏后 X 回到行首：若仍用 (X0,X1) 轴对齐包围盒，
     * max(X0,X1)+Advance 只盖住行首约一字，Compose 会把同行其余字盖掉 →「吞字」。
     */
    if (Y1 != Y0 || X1 < X0) {
        if (GuiFocusClient(&Cx, &Cy, &Cw, &Ch, &Bg) && Cw > 0 && Ch > 0) {
            if (Y1 < Y0) {
                /* 滚屏：整客户区 */
                GuiBackupSyncRect(Cx, Cy, Cw, Ch);
            } else {
                T = Y0 < Y1 ? Y0 : Y1;
                B = (Y0 > Y1 ? Y0 : Y1) + FontAdvanceY();
                if (B > Cy + Ch) {
                    B = Cy + Ch;
                }
                if (T < Cy) {
                    T = Cy;
                }
                if (B > T) {
                    GuiBackupSyncRect(Cx, T, Cw, B - T);
                }
            }
        }
    } else {
        L = X0 < X1 ? X0 : X1;
        T = Y0;
        R = (X0 > X1 ? X0 : X1) + FontAdvanceX();
        B = Y0 + FontAdvanceY();
        if (R > L && B > T) {
            GuiBackupSyncRect(L, T, R - L, B - T);
        }
    }
    GuiFocusSyncCursor();
    GuiFrameBufferEnd();
}

void ConsoleDrawChar(char C, UINT32 Color) {
    UINT32 X;
    UINT32 Y;

    ConsoleOutLock();
    GuiFrameBufferBegin();
    GuiFocusApplyClip();
    HalConsoleGetTextCursor(&X, &Y);
    FontTtfMonoBegin();
    HalConsoleDrawChar(C, Color);
    FontTtfMonoEnd();
    GuiBackupSyncRect(X, Y, FontCellW(), FontCellH());
    GuiFocusSyncCursor();
    /* PR-G-shell-present：打字回显合并 Present（ShellTask 轮询末刷） */
    GuiPresentShellEchoMark();
    GuiFrameBufferEnd();
    ConsoleOutUnlock();
}

/*
 * 仅 Shell 窗：scrollback + 帧缓冲。开窗欢迎语走这里，避免串口重复
 * 「Blocks console. Type help.」（串口已有 blocks> / ready）。
 */
void ConsoleWriteShellScreen(const char *Text) {
    const char *P;

    if (Text == 0 || !HalConsoleVideoReady()) {
        return;
    }
    if (GuiFocusKind() != GUI_WIN_SHELL) {
        return;
    }
    ConsoleOutLock();
    for (P = Text; *P; P++) {
        gAtLineStart = (*P == '\n');
    }
    ConsoleSbBindFocus();
    ConsoleSbEnsureLive();
    ConsoleSbFeed(Text);
    if (GuiShellAcceptsInput() || GuiShellWindowActive(GuiFocusIndex())) {
        ConsoleDrawString(Text, ThemeShellText());
    }
    ConsoleSbBarAfterWrite();
    ConsoleOutUnlock();
}

/* 同时输出到串口与屏幕（帧缓冲未就绪时只写串口） */
static void Write(const char *Text) {
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
     * Hello 等控制台 ELF（非 StdoutSerialOnly）：Drain 内只串口+scrollback+缓冲；
     * 禁止 syscall 里 ConsoleDrawString→PresentFlush（真机打一字卡死）。
     * 屏上 Hello：CoopUserOutFlush（Drain 后宿主 Write）。
     */
    if (T && T->IsUser) {
        int Owner;

        for (P = Text; *P; P++) {
            gAtLineStart = (*P == '\n');
        }
        HalConsoleWriteSerial(Text);
        if (gCoopDrain) {
            CoopUserOutAppend(Text);
        }
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
         * 用户窗（Snake/TaskMgr…）焦点在 USER：stdout 不进教学串口、也不 Feed Shell。
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
static void WriteLen(const char *Data, UINTN Len) {
    UINTN i;

    if (Data == 0 || Len == 0) {
        return;
    }
    /*
     * 整段持锁：逐码点 Write 时勿让 Prompt 插队（见 Console.c Prompt）。
     * Write 自身也加锁 → 此处不锁，改由外层包住会嵌套死锁；故 WriteLen
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

/* 输出 32 位十六进制 */
static void WriteHex32(UINT32 Value) {
    char Buf[12];
    HalSerialFormatHex(Buf, Value, 8);
    gConsole.Write(Buf);
}

/* 输出 64 位十六进制 */
static void WriteHex64(UINT64 Value) {
    char Buf[20];
    HalSerialFormatHex(Buf, Value, 16);
    gConsole.Write(Buf);
}

void ConsolePublishWrite(CONSOLE_OPS *Ops) {
    Ops->Write = Write;
    Ops->WriteLen = WriteLen;
    /* CoopUserOut* 经 CoreOps CONSOLE_IO_OPS 登记，见 Focus.c */
    Ops->WriteHex32 = WriteHex32;
    Ops->WriteHex64 = WriteHex64;
}
