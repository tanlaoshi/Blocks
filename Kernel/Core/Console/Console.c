/*
 * Console.c — 串口壳 + K15 PS/2 键入
 *
 * 【初学者】
 * 读行同时 poll COM1 与 PS/2（GTK 窗按键走 i8042）。
 * K22：命令表见 ShellCmd（help/clear/echo/hello）。
 */
#include "Console.h"
#include "Gui.h"
#include "HalPs2.h"
#include "HalPs2Kbd.h"
#include "HalPs2Mouse.h"
#include "HalSerial.h"
#include "HalVideo.h"
#include "Process.h"
#include "ShellCmd.h"
#include "Scheduler.h"
#include "Font.h"
#include "Locale.h"
#include "Theme.h"
#include "Network.h"
#include "LwIp.h"
#include "SerialConfig.h"

#define LINE_CAP 120

static void ConsolePut(const char *Text) {
    HalSerialWriteShell(Text);
}

void ConsolePaintBannerBack(void) {
    UINT32 Cx = 0;
    UINT32 Cy = 0;
    UINT32 Cw = 0;
    UINT32 Ch = 0;
    UINT32 Pad = 8u;

    ThemeInitialize();
    LocaleInitialize();
    if (GuiShellClientRect(&Cx, &Cy, &Cw, &Ch) != 0 || Cw < 64u || Ch < 40u) {
        return;
    }
    HalVideoFillRect(Cx, Cy, Cw, Ch, ThemeWindowClient());
    FontDrawStringAt(Cx + Pad, Cy + Pad, LocStr(MSG_READY), ThemeTextForeground());
    FontDrawStringAt(Cx + Pad, Cy + Pad + 20u, "Blocks>", ThemeTextForeground());
}

void ConsoleRefreshBanner(void) {
    GuiCursorHide();
    ConsolePaintBannerBack();
    {
        UINT32 Cx = 0;
        UINT32 Cy = 0;
        UINT32 Cw = 0;
        UINT32 Ch = 0;
        if (GuiShellClientRect(&Cx, &Cy, &Cw, &Ch) == 0 && Cw >= 64u && Ch >= 40u) {
            HalVideoPresentRect(Cx, Cy, Cw, Ch);
        }
    }
    GuiCursorShow();
}

int ConsoleInitialize(void) {
    HalSerialWriteChannel(SLOG_BOOT, "Blocks ready\n");
    if (HalPs2KbdInit() == 0 && HalPs2KbdReady()) {
        HalSerialWriteChannel(SLOG_MISC, "Input: ps2 kbd ok\n");
    } else {
        HalSerialWriteChannel(SLOG_MISC, "Input: ps2 skip (serial only)\n");
    }
    ShellCmdInitialize();
    ConsoleRefreshBanner();
    HalSerialWriteChannel(SLOG_MISC, "Console: init ok\n");
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
        int Have = 0;
        /*
         * 无键时：先紧轮 Gui 再 Yield。
         * PS/2 鼠无 IRQ，若每拍只 poll 一次就 hlt，光标会不跟手。
         */
        while (!Have) {
            if (ConsolePollChar(&C)) {
                Have = 1;
                break;
            }
            /*
             * 鼠无 IRQ：有 PS/2 鼠时禁止 hlt。
             * 一睡 i8042 就溢、失步 → 移动卡 / 假死。
             */
#if defined(__x86_64__) || defined(_M_X64)
            HalPs2Poll();
#endif
            (void)GuiPoll();
            /* K41：lwIP 活跃时统一 RX；否则 K40 builtin TCP */
            if (LwIpActive()) {
                LwIpService();
            } else if (NetworkTcpGetState() != NETWORK_TCP_CLOSED) {
                NetworkTcpPoll(0);
            }
            if (ConsolePollChar(&C)) {
                Have = 1;
                break;
            }
#if defined(__x86_64__) || defined(_M_X64)
            if (HalPs2MouseReady()) {
                __asm__ volatile("pause");
                continue;
            }
#endif
            SchedulerYield();
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
        ShellCmdRunLine(Line);
    }
}
