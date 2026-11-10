/*
 * ConsoleStdin.c — 用户 stdin 环 / AnnounceBootReady（自 Console.c 纯搬 · PR-NORM-16）
 */
#include "Console.h"
#include "ConsolePrivate.h"
#include "UI.h"
#include "Hal.h"
#include "Gui.h"
#include "Locale.h"
#include "Scheduler.h"
#include "SerialLog.h"

/* 用户 ELF read(0)：Shell 泄串口/键盘入环；须 ≥ 真机单轮 MaxRx，防粘贴丢字 */
#define STDIN_Q_CAP 256
static int gBootReadyAnnounced;
static char gStdinQ[STDIN_Q_CAP];
static int gStdinR;
static int gStdinW;
static int gStdinN;

int StdinUserHold(void) {
    return gWaitPrompt > 0 || SchedulerLiveUserApps();
}

void StdinFlush(void) {
    gStdinR = 0;
    gStdinW = 0;
    gStdinN = 0;
}

void StdinPut(char C) {
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

char StdinGetChar(void) {
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

/* 桌面：Worker 末档调用；SerialRun 亦可。once。契约见 Tasks/Shell.c 头。 */
void AnnounceBootReady(void) {
    if (gBootReadyAnnounced) {
        return;
    }
    gBootReadyAnnounced = 1;
    HalConsoleWriteSerial(LocaleString(MESSAGE_CON_READY));
    HalConsoleWriteSerial("\n");
    HalConsoleWriteSerial("hint: type commands in THIS terminal (not QEMU window)\n");
    Prompt();
    HalSerialShellOwn();
}
