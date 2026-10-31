/*
 * Ksend.c — 壳 `ksend [--no-reboot] [info|selftest|<目标路径>]`：串口增量写卡
 *
 * 实现在 HAL/Common/Ksend/；弱引用三符号（无链入时提示 not built）。
 * v2：目标路径；v3：--no-reboot；v4：--no-backup。
 */
#include "ShellPrivate.h"
#include "Console.h"

extern int KsendInfo(void) __attribute__((weak));
extern int KsendSelftest(void) __attribute__((weak));
extern int KsendTransfer(const char *TargetPath, int Reboot, int Backup)
    __attribute__((weak));

static int KsendArgumentIs(const char *Argument, const char *Word) {
    if (Argument == 0) {
        return 0;
    }
    while (*Argument != 0 && *Word != 0) {
        char A = *Argument;
        char W = *Word;

        if (A >= 'A' && A <= 'Z') {
            A = (char)(A - 'A' + 'a');
        }
        if (W >= 'A' && W <= 'Z') {
            W = (char)(W - 'A' + 'a');
        }
        if (A != W) {
            return 0;
        }
        Argument++;
        Word++;
    }
    return *Argument == 0 && *Word == 0;
}

static void CommandKsend(int Argc, char **Argv) {
    int Index = 1;
    int Reboot = 1;
    int Backup = 1;

    if (KsendInfo == 0 || KsendSelftest == 0 || KsendTransfer == 0) {
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
    if (Index < Argc) {
        if (KsendArgumentIs(Argv[Index], "info")) {
            (void)KsendInfo();
            return;
        }
        if (KsendArgumentIs(Argv[Index], "selftest")) {
            (void)KsendSelftest();
            return;
        }
        if (Index + 1 == Argc) {
            (void)KsendTransfer(Argv[Index], Reboot, Backup);
            return;
        }
        gConsole.Write("usage: ksend [--no-reboot] [--no-backup] "
                       "[info|selftest|<target path>]\n");
        return;
    }
    (void)KsendTransfer(0, Reboot, Backup);
}

void ShellCommandsKsendRegister(void) {
    gConsole.Register(
        "ksend",
        "ksend [--no-reboot] [--no-backup] [info|selftest|<target path>] "
        "— serial delta file write",
        CommandKsend);
}
