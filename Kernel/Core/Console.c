/*
 * Console.c — K8：串口壳（横幅 + 提示符 + 回显一行）
 *
 * 【初学者】
 * 不是完整 Shell：不解析命令表。读到回车就原样打回，证明 RX 通路活着。
 * 屏上另打一行字（无自检色块）：证明 LFB/背缓冲仍活。
 */
#include "Console.h"
#include "HalSerial.h"
#include "HalVideo.h"
#include "Scheduler.h"
#include "ToySerialConfig.h"

#define LINE_CAP 120

static void ConsolePut(const char *Text) {
    /* ShellOwn 后走 WriteShell，避免被日志通道关掉 TX */
    HalSerialWriteShell(Text);
}

/* 左上角白字；背缓冲开着则 Present，否则直写 LFB 已可见 */
static void ConsolePaintBanner(void) {
    UINT32 W = 0;
    UINT32 H = 0;

    HalVideoGetSize(&W, &H);
    if (W < 96 || H < 40) {
        return;
    }
    HalVideoDrawStringAt(8, 8, "ToyOS ready", 0x00FFFFFFu);
    HalVideoDrawStringAt(8, 24, "Blocks>", 0x00FFFFFFu);
    if (HalVideoBackbufferEnabled()) {
        HalVideoPresent();
    }
}

int ConsoleInitialize(void) {
    /* 横幅仍走日志通道；进 Run 再 ShellOwn，独占提示符输入 */
    HalSerialWriteChannel(TOY_SLOG_BOOT, "ToyOS ready\n");
    ConsolePaintBanner();
    return 0;
}

static int ConsoleReadLine(char *Buf, int Cap) {
    int N = 0;
    char C;

    if (Buf == 0 || Cap <= 1) {
        return -1;
    }
    for (;;) {
        if (!HalSerialDataReady()) {
            SchedulerYield();
            continue;
        }
        C = HalSerialReadChar();
        /* 终端/QEMU 常只送 '\\r'；二者都算行结束（\\r\\n 会多一次空行，可忽略） */
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

void ConsoleRun(void) {
    char Line[LINE_CAP];

    HalSerialShellOwn();
    for (;;) {
        ConsolePut("Blocks> ");
        if (ConsoleReadLine(Line, LINE_CAP) < 0) {
            continue;
        }
        if (Line[0] == 0) {
            continue;
        }
        ConsolePut(Line);
        ConsolePut("\n");
    }
}
