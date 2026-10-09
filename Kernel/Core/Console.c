/*
 * Console.c — 串口壳 + K15 PS/2 键入
 *
 * 【初学者】
 * 读行同时 poll COM1 与 PS/2（GTK 窗按键走 i8042）。
 * 不是完整 Shell：回车后原样打回。
 */
#include "Console.h"
#include "Gui.h"
#include "HalPs2Kbd.h"
#include "HalSerial.h"
#include "HalVideo.h"
#include "Process.h"
#include "Scheduler.h"
#include "ToySerialConfig.h"

#define LINE_CAP 120

static void ConsolePut(const char *Text) {
    HalSerialWriteShell(Text);
}

static void ConsolePaintBanner(void) {
    UINT32 W = 0;
    UINT32 H = 0;

    HalVideoGetSize(&W, &H);
    if (W < 96 || H < 40) {
        return;
    }
    HalVideoDrawStringAt(8, 40, "ToyOS ready", 0x00FFFFFFu);
    HalVideoDrawStringAt(8, 56, "Blocks>", 0x00FFFFFFu);
    if (HalVideoBackbufferEnabled()) {
        HalVideoPresent();
    }
}

int ConsoleInitialize(void) {
    HalSerialWriteChannel(TOY_SLOG_BOOT, "ToyOS ready\n");
    if (HalPs2KbdInit() == 0 && HalPs2KbdReady()) {
        HalSerialWriteChannel(TOY_SLOG_MISC, "Input: ps2 kbd ok\n");
    } else {
        HalSerialWriteChannel(TOY_SLOG_MISC, "Input: ps2 skip (serial only)\n");
    }
    ConsolePaintBanner();
    return 0;
}

/* 串口优先，否则 PS/2；都无则 0 */
static int ConsolePollChar(char *Out) {
    if (Out == 0) {
        return 0;
    }
    if (HalSerialDataReady()) {
        *Out = HalSerialReadChar();
        return 1;
    }
    if (HalPs2KbdPollChar(Out)) {
        return 1;
    }
    return 0;
}

static int ConsoleReadLine(char *Buf, int Cap) {
    int N = 0;
    char C;

    if (Buf == 0 || Cap <= 1) {
        return -1;
    }
    for (;;) {
        if (!ConsolePollChar(&C)) {
            GuiPoll();
            SchedulerYield();
            continue;
        }
        if (C == '\r' || C == '\n') {
            Buf[N] = 0;
            ConsolePut("\n");
            return N;
        }
        if (C == 0x7f || C == '\b') {
            if (N > 0) {
                N--;
                ConsolePut("\b \b");
            }
            continue;
        }
        if (C < 0x20 || C > 0x7e) {
            continue;
        }
        if (N + 1 >= Cap) {
            continue;
        }
        Buf[N++] = C;
        {
            char One[2];
            One[0] = C;
            One[1] = 0;
            ConsolePut(One);
        }
    }
}

static int LineEq(const char *A, const char *B) {
    while (*A && *B && *A == *B) {
        A++;
        B++;
    }
    return *A == 0 && *B == 0;
}

void ConsoleRun(void) {
    char Line[LINE_CAP];

    HalSerialShellOwn();
    /* K19：开机跑一次 HELLO.ELF，再进提示符 */
    (void)ProcessRunHello();
    for (;;) {
        ConsolePut("Blocks> ");
        if (ConsoleReadLine(Line, LINE_CAP) < 0) {
            continue;
        }
        if (Line[0] == 0) {
            continue;
        }
        if (LineEq(Line, "hello")) {
            (void)ProcessRunHello();
            continue;
        }
        ConsolePut(Line);
        ConsolePut("\n");
    }
}
