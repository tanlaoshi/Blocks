/*
 * Ksend.c — 壳增量写卡：串口（备份）或 `ksend serve`（TCP 网络热更）
 *
 * 实现在 HAL/Common/Ksend/；弱引用（无链入时提示 not built）。
 */
#include "ShellPrivate.h"
#include "Console.h"
#include "BootTypes.h"
#include "Services/Gui.h"
#include "Hal.h"

extern int KsendInfo(void) __attribute__((weak));
extern int KsendSelfTest(void) __attribute__((weak));
extern int KsendTransfer(const char *TargetPath, int Reboot, int Backup)
    __attribute__((weak));
extern int KsendServe(UINT16 Port, UINT32 WaitMilliseconds) __attribute__((weak));
extern UINT16 KsendServeDefaultPort(void) __attribute__((weak));

static int KsendArgumentIs(const char *Argument, const char *Word) {
    if (Argument == 0) {
        return 0;
    }
    while (*Argument != 0 && *Word != 0) {
        char Left = *Argument;
        char Right = *Word;

        if (Left >= 'A' && Left <= 'Z') {
            Left = (char)(Left - 'A' + 'a');
        }
        if (Right >= 'A' && Right <= 'Z') {
            Right = (char)(Right - 'A' + 'a');
        }
        if (Left != Right) {
            return 0;
        }
        Argument++;
        Word++;
    }
    return *Argument == 0 && *Word == 0;
}

static UINT32 ParseDecimal(const char *Text) {
    UINT32 Value = 0;

    if (Text == 0) {
        return 0;
    }
    while (*Text >= '0' && *Text <= '9') {
        Value = Value * 10u + (UINT32)(*Text - '0');
        Text++;
    }
    return Value;
}

static void CommandKsend(int Argc, char **Argv) {
    int Index = 1;
    int Reboot = 1;
    int Backup = 1;
    UINT16 Port;
    UINT32 WaitMilliseconds;

    if (KsendInfo == 0 || KsendSelfTest == 0 || KsendTransfer == 0) {
        gConsole.Write("ksend: not built for this board\n");
        return;
    }
    while (Index < Argc) {
        if (KsendArgumentIs(Argv[Index], "--no-reboot")) {
            Reboot = 0;
        } else if (KsendArgumentIs(Argv[Index], "--no-backup")) {
            Backup = 0;
        } else {
            break;
        }
        Index++;
    }
    if (Index < Argc && KsendArgumentIs(Argv[Index], "serve")) {
        if (KsendServe == 0 || KsendServeDefaultPort == 0) {
            gConsole.Write("ksend: serve not built\n");
            return;
        }
        Port = KsendServeDefaultPort();
        WaitMilliseconds = 0;
        Index++;
        if (Index < Argc) {
            Port = (UINT16)ParseDecimal(Argv[Index]);
            Index++;
        }
        if (Index < Argc) {
            WaitMilliseconds = ParseDecimal(Argv[Index]);
        }
        gConsole.Write("ksend: serve (TCP; wired/wifi via lwIP). PC: "
                       "ksend.py --host <ip>\n");
        /*
         * Write→EchoMark 指望 Shell 环末 Present；Serve 同线程阻塞，
         * 字停在后缓冲直到鼠动 Flush（Pi 2026-11-03）。Pause 清 coalesce 再硬刷。
         */
        GuiPresentDeferPause();
        HalVideoPresent();
        GuiPresentDeferResume();
        (void)Reboot;
        (void)Backup;
        (void)KsendServe(Port, WaitMilliseconds);
        return;
    }
    if (Index < Argc) {
        if (KsendArgumentIs(Argv[Index], "info")) {
            (void)KsendInfo();
            return;
        }
        if (KsendArgumentIs(Argv[Index], "selftest")) {
            (void)KsendSelfTest();
            return;
        }
        if (Index + 1 == Argc) {
            (void)KsendTransfer(Argv[Index], Reboot, Backup);
            return;
        }
        gConsole.Write("usage: ksend [--no-reboot] [--no-backup] "
                       "[info|selftest|serve [port] [wait-ms]|<target path>]\n");
        return;
    }
    (void)KsendTransfer(0, Reboot, Backup);
}

void ShellCommandsKsendRegister(void) {
    gConsole.Register(
        "ksend",
        "ksend [serve [port]|info|selftest|<path>] — serial or TCP delta write",
        CommandKsend);
}
