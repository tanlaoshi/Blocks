/*
 * Console.c — 串口壳 + 屏上提示符（读行、分发 Shell）
 *
 * 【初学者】
 * - 分层：Core/Console 主文件；命令实现见 ShellCommand/
 * - 对外入口：ConsoleInitialize、ConsoleRun、ConsoleRefreshBanner
 * - 读行同时 poll COM1 / USB HID / PS/2；无键时 GuiPoll + 网络 Service + SchedulerYield
 * - 不做：命令语义（在 ShellCommand*）；用户 ELF 装载见 Process.c
 */
#include "Console.h"
#include "Gui.h"
#include "HalPs2.h"
#include "HalPs2Keyboard.h"
#include "HalPs2Mouse.h"
#include "HalUsbHid.h"
#include "HalSerial.h"
#include "HalVideo.h"
#include "Process.h"
#include "ShellCommand.h"
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

/*
 * ConsolePaintBannerBack — 重画 Shell 客户区背景与「Blocks>」提示条
 *
 * 做什么：Theme/Locale 就绪后填充 Gui 壳客户区并写就绪文案。
 * 谁调用：ConsoleRefreshBanner；ConsoleInitialize 经 RefreshBanner。
 * 前后文：前 — ThemeInitialize / LocaleInitialize；后 — HalVideoPresentRect（Refresh 内）
 * 返回：void（矩形无效则早退）
 */
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

/*
 * ConsoleRefreshBanner — 无闪烁刷新 Shell 横幅区
 *
 * 做什么：藏光标 → 重画背景 → Present 客户区 → 恢复光标。
 * 谁调用：ConsoleInitialize；Shell `lang`（Theme.c）；Gui 改语言后。
 * 返回：void
 */
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

/*
 * ConsoleInitialize — 控制台与 Shell 命令表就绪
 *
 * 做什么：日志就绪、探测键盘、ShellCommandInitialize、画横幅。
 * 谁调用：Modules.c 启动表「Console」项（KernelMain 链）。
 * 前后文：前 — Network/Gui 等模块；后 — ConsoleRun 或 Gui 壳并存
 * 返回：0
 */
int ConsoleInitialize(void) {
    HalSerialWriteChannel(SLOG_BOOT, "Blocks ready\n");
    if (HalUsbHidKeyboardReady()) {
        HalSerialWriteChannel(SLOG_MISC, "Input: hid kbd ok\n");
    } else if (HalPs2KeyboardInitialize() == 0 && HalPs2KeyboardReady()) {
        HalSerialWriteChannel(SLOG_MISC, "Input: ps2 kbd ok\n");
    } else {
        HalSerialWriteChannel(SLOG_MISC, "Input: ps2 skip (serial only)\n");
    }
    ShellCommandInitialize();
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
    if (HalUsbHidPollChar(Out)) {
        return 1;
    }
    if (HalPs2KeyboardPollChar(Out)) {
        return 1;
    }
    return 0;
}

/*
 * ConsoleReadLine — 阻塞读一行到 LineBuffer（含退格、过滤控制符）
 *
 * 做什么：轮询输入；空闲时 Gui/网络/调度；遇 CR/LF 结束。
 * 谁调用：仅 ConsoleRun。
 * 前后文：兄弟 — ConsolePollChar
 * 返回：字符数；Cap 非法时 -1
 */
static int ConsoleReadLine(char *LineBuffer, int Capacity) {
    int N = 0;
    char C;

    if (LineBuffer == 0 || Capacity <= 1) {
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
            HalUsbHidService();
            HalPs2Poll();
#endif
            (void)GuiPoll();
            /* K41：lwIP 活跃时统一 RX；否则 K40 builtin TCP */
            if (LwIpActive()) {
                LwIpService();
            } else if (TcpGetState() != NETWORK_TCP_CLOSED) {
                TcpPoll(0);
            }
            if (ConsolePollChar(&C)) {
                Have = 1;
                break;
            }
#if defined(__x86_64__) || defined(_M_X64)
            if (HalUsbHidMouseReady() || HalPs2MouseReady()) {
                __asm__ volatile("pause");
                continue;
            }
#endif
            SchedulerYield();
        }
        if (C == '\r' || C == '\n') {
            LineBuffer[N] = 0;
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
        if (N + 1 >= Capacity) {
            continue;
        }
        LineBuffer[N++] = C;
        {
            char One[2];
            One[0] = C;
            One[1] = 0;
            ConsolePut(One);
        }
    }
}


/*
 * ConsoleRun — 永久 Shell 读行循环
 *
 * 做什么：接管串口 Shell；可选先 ProcessRunHello；每行 ShellCommandRunLine。
 * 谁调用：KernelMain / 启动路径（Gui 与 Shell 二选一或串行）。
 * 返回：void（不返回）
 */
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
        ShellCommandRunLine(Line);
    }
}
