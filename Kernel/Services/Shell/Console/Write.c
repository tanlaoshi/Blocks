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
/* run user：done 须在 Hello 之后；且须 DeferPop 后再 Write（见 Flush） */
static int gCoopUserOutTrailRunUserDone;

void ConsoleCoopUserOutClear(void) {
    gCoopUserOutLen = 0;
    gCoopUserOutTrailRunUserDone = 0;
}

void ConsoleCoopUserOutArmRunUserDone(void) {
    gCoopUserOutTrailRunUserDone = 1;
}

void ConsoleCoopUserOutAppend(const char *Text) {
    if (!Text) {
        return;
    }
    while (*Text && gCoopUserOutLen + 1u < COOP_USER_OUT_MAX) {
        gCoopUserOut[gCoopUserOutLen++] = *Text++;
    }
}

void ConsoleCoopUserOutFlush(void) {
    if (gCoopUserOutLen != 0) {
        gCoopUserOut[gCoopUserOutLen] = 0;
        gCoopUserOutLen = 0;
        /* 宿主已是 Shell 内核任务：走完整 Write（串口+屏） */
        gConsole.Write(gCoopUserOut);
    }
    /*
     * 顺序：… → Hello（上）→ done（本尾）；调用方须在 DeferPop 前 Flush，
     * 避免 PresentFlush 与 Worker 网初始化抢 xHCI（NUC 二次鼠/电源死）。
     */
    if (gCoopUserOutTrailRunUserDone) {
        gCoopUserOutTrailRunUserDone = 0;
        gConsole.Write("run user: done\n");
    }
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
    /* ASCII 与桌面同走 Latin 点阵（Terminus）；勿开 MONO.TTF */
    HalConsoleDrawString(Text, Color);
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
    HalConsoleDrawChar(C, Color);
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

/* Write/WriteLength → WriteCore.c（PR-NORM-16）。 */

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
    Ops->Write = ConsoleWriteText;
    Ops->WriteLength = ConsoleWriteLength;
    /* CoopUserOut* 经 CoreOps CONSOLE_IO_OPS 登记，见 Focus.c */
    Ops->WriteHex32 = WriteHex32;
    Ops->WriteHex64 = WriteHex64;
}
