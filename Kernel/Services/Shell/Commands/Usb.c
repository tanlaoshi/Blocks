/*
 * ShellCommandsUsb.c — PR-S-shell-split-1：xhci / msc Shell 命令
 *
 * 从 ShellCommands.c 原样搬家；msc mount = PR-H-msc-6。
 */
#include "ShellPrivate.h"
#include "Console.h"
#include "Hal.h"
#include "FileSystem.h"

/* PR-H-xhci-stat / PR-V-input-diag：Shell 可查 mode= 或 virtio 计数 */
static void CommandInputDiag(int Argc, char **Argv) {
    char Diag[160];

    (void)Argc;
    (void)Argv;
    Diag[0] = 0;
    HalInputDiagFormat(Diag, (int)sizeof(Diag));
    gConsole.Write("input ");
    gConsole.Write(Diag[0] ? Diag : "(no stats)");
    gConsole.Write("\n");
}

static void CommandXhci(int Argc, char **Argv) {
    char Diag[160];

    (void)Argc;
    (void)Argv;
    Diag[0] = 0;
    HalInputDiagFormat(Diag, (int)sizeof(Diag));
    gConsole.Write("xhci ");
    gConsole.Write(Diag[0] ? Diag : "(no stats)");
    gConsole.Write("\n");
}

/* PR-H-ehci-1/2/4：诊断；`ehci hid` 重枚举；`ehci ftdi` ping tee */
static void CommandEhci(int Argc, char **Argv) {
    char Diag[160];

    if (Argc >= 2 && Argv[1] && Argv[1][0] == 'h' && Argv[1][1] == 'i' &&
        Argv[1][2] == 'd' && Argv[1][3] == 0) {
        gConsole.Write("ehci: hid retry…\n");
        if (HalEhciHidRetry() == 0) {
            gConsole.Write("ehci: hid ok\n");
        } else {
            gConsole.Write("ehci: hid fail — ");
        }
        Diag[0] = 0;
        HalEhciDiagFormat(Diag, (int)sizeof(Diag));
        gConsole.Write(Diag[0] ? Diag : "(no stats)");
        gConsole.Write("\n");
        return;
    }
    if (Argc >= 2 && Argv[1] && Argv[1][0] == 'f' && Argv[1][1] == 't' &&
        Argv[1][2] == 'd' && Argv[1][3] == 'i' && Argv[1][4] == 0) {
        int Rc = HalEhciFtdiPing();
        if (Rc == 1) {
            gConsole.Write("ehci: ftdi bulk ok — CoolTerm 115200 8N1 expect *** FTDI ***\n");
        } else if (Rc == 0) {
            gConsole.Write("ehci: ftdi=0 (not claimed)\n");
        } else {
            gConsole.Write("ehci: ftdi bulk FAIL — ");
            Diag[0] = 0;
            HalEhciDiagFormat(Diag, (int)sizeof(Diag));
            gConsole.Write(Diag[0] ? Diag : "?");
            gConsole.Write("\n");
        }
        return;
    }

    Diag[0] = 0;
    HalEhciDiagFormat(Diag, (int)sizeof(Diag));
    gConsole.Write("ehci ");
    gConsole.Write(Diag[0] ? Diag : "(no stats)");
    gConsole.Write("\n");
}

/* PR-H-uhci-1：UHCI CCS */
static void CommandUhci(int Argc, char **Argv) {
    char Diag[160];

    (void)Argc;
    (void)Argv;
    Diag[0] = 0;
    HalUhciDiagFormat(Diag, (int)sizeof(Diag));
    gConsole.Write("uhci ");
    gConsole.Write(Diag[0] ? Diag : "(no stats)");
    gConsole.Write("\n");
}

/* PR-H-ps2-aux：触控板诊断；ps2 retry 重开 Aux */
static void CommandPs2(int Argc, char **Argv) {
    char Diag[160];

    if (Argc >= 2 && Argv[1] && Argv[1][0] == 'r') {
        gConsole.Write("ps2 Retry ");
        gConsole.Write(HalPs2AuxRetry() ? "Ok\n" : "Fail\n");
    }
    Diag[0] = 0;
    HalPs2DiagFormat(Diag, (int)sizeof(Diag));
    gConsole.Write("ps2 ");
    gConsole.Write(Diag[0] ? Diag : "(no stats)");
    gConsole.Write("\n");
    gConsole.Write("  (aux=1 后滑板看 pkts↑；fail=ff/bat/f4=Init 卡点；input 看总鼠)\n");
}

/* PR-H-msc-6：msc | scan | claim | capacity | mount；不自动认盘 */
static void CommandMsc(int Argc, char **Argv) {
    int Rc;

    if (Argc >= 2 && Argv[1] && Argv[1][0] == 's' && Argv[1][1] == 'c' &&
        Argv[1][2] == 'a' && Argv[1][3] == 'n' && Argv[1][4] == 0) {
        Rc = HalUsbMscScan();
        gConsole.Write("msc: scan ");
        if (Rc < 0) {
            gConsole.Write("fail (no hc)\n");
        } else {
            gConsole.Write("ok n=");
            gConsole.WriteHex32((UINT32)Rc);
            gConsole.Write(" (PORTSC only; no Address; claim for class/BOT)\n");
        }
        return;
    }

    if (Argc >= 2 && Argv[1] && Argv[1][0] == 'c' && Argv[1][1] == 'l' &&
        Argv[1][2] == 'a' && Argv[1][3] == 'i' && Argv[1][4] == 'm' &&
        Argv[1][5] == 0) {
        Rc = HalUsbMscClaim();
        gConsole.Write("msc: claim ");
        if (Rc < 0) {
            gConsole.Write("fail (no hc)\n");
        } else if (Rc == 0) {
            gConsole.Write("none (no MSC bulk port; HID untouched)\n");
        } else {
            gConsole.Write("ok ready=");
            gConsole.Write(HalUsbMscReady() ? "1" : "0");
            gConsole.Write(" (SetConfig+Bulk; use: msc capacity|mount; HID untouched)\n");
        }
        return;
    }

    if (Argc >= 2 && Argv[1] && Argv[1][0] == 'c' && Argv[1][1] == 'a' &&
        Argv[1][2] == 'p' && Argv[1][3] == 'a' && Argv[1][4] == 'c' &&
        Argv[1][5] == 'i' && Argv[1][6] == 't' && Argv[1][7] == 'y' &&
        Argv[1][8] == 0) {
        Rc = HalUsbMscCapacity();
        gConsole.Write("msc: capacity ");
        if (Rc < 0) {
            gConsole.Write("fail (need claim; no FAT)\n");
        } else {
            gConsole.Write("ok blocks=");
            gConsole.WriteHex32(HalUsbMscBlockCount());
            gConsole.Write(" bsize=");
            gConsole.WriteHex32(HalUsbMscBlockSize());
            gConsole.Write(" (INQUIRY+READ CAPACITY; no partition/FAT; HID untouched)\n");
        }
        return;
    }

    if (Argc >= 2 && Argv[1] && Argv[1][0] == 'm' && Argv[1][1] == 'o' &&
        Argv[1][2] == 'u' && Argv[1][3] == 'n' && Argv[1][4] == 't' &&
        Argv[1][5] == 0) {
        Rc = HalUsbMscMount();
        gConsole.Write("msc: mount ");
        if (Rc == -1) {
            gConsole.Write("fail (need claim)\n");
            return;
        }
        if (Rc == -2) {
            gConsole.Write("fail (capacity)\n");
            return;
        }
        if (Rc == -3) {
            gConsole.Write("fail (bsize!=512; FAT needs 512)\n");
            return;
        }
        if (Rc != 0) {
            gConsole.Write("fail\n");
            return;
        }
        gConsole.Write("mux ok bsize=512; remount…\n");
        if (!FileSystemRemountVolumes()) {
            gConsole.Write("msc: remount fail (no FAT vols; HID untouched)\n");
            return;
        }
        gConsole.Write("msc: remount ok vols=");
        gConsole.WriteHex32((UINT32)FileSystemVolCount());
        gConsole.Write(" default=");
        {
            char Name[FS_VOL_NAME_MAX];
            int Def = FileSystemDefaultVol();

            Name[0] = 0;
            if (Def >= 0) {
                (void)FileSystemVolInfo(Def, Name, FS_VOL_NAME_MAX, 0, 0, 0);
            }
            gConsole.Write(Name[0] ? Name : "?");
        }
        gConsole.Write(" (try: vols | ls BLOCKS:; HID untouched)\n");
        return;
    }

    if (Argc >= 2 && Argv[1] && Argv[1][0] == 'r' && Argv[1][1] == 'e' &&
        Argv[1][2] == 'l' && Argv[1][3] == 'e' && Argv[1][4] == 'a' &&
        Argv[1][5] == 's' && Argv[1][6] == 'e' && Argv[1][7] == 0) {
        (void)HalUsbMscRelease();
        gConsole.Write("msc: release ok; remount…\n");
        if (!FileSystemRemountVolumes()) {
            gConsole.Write("msc: remount fail (primary/RES may remain)\n");
            return;
        }
        gConsole.Write("msc: remount ok vols=");
        gConsole.WriteHex32((UINT32)FileSystemVolCount());
        gConsole.Write("\n");
        return;
    }

    if (Argc >= 2 && Argv[1] && Argv[1][0] == 'h' && Argv[1][1] == 'o' &&
        Argv[1][2] == 't' && Argv[1][3] == 0) {
        Rc = HalUsbMscHot();
        gConsole.Write("msc: hot ");
        if (Rc == 1) {
            gConsole.Write("none (no MSC; try plug then hot)\n");
            return;
        }
        if (Rc < 0) {
            gConsole.Write("fail\n");
            return;
        }
        gConsole.Write("mux ok; remount…\n");
        if (!FileSystemRemountVolumes()) {
            gConsole.Write("msc: remount fail\n");
            return;
        }
        gConsole.Write("msc: remount ok vols=");
        gConsole.WriteHex32((UINT32)FileSystemVolCount());
        gConsole.Write("\n");
        return;
    }

    Rc = HalUsbMscInitialize();
    gConsole.Write("msc: bringup=");
    gConsole.Write(Rc == 0 ? "ok" : "fail");
    gConsole.Write(" ready=");
    gConsole.Write(HalUsbMscReady() ? "1" : "0");
    gConsole.Write(" auto=");
    gConsole.Write(HalUsbMscAutoEnabled() ? "1" : "0");
    gConsole.Write(" (scan|claim|capacity|mount|release|hot; hot=晚插挂盘; auto=开机认盘 THEME msc=0|MSC.OFF)\n");
}

void ShellCommandsUsbRegister(void) {
    gConsole.Register2("show", "xhci", "xHCI mode= + counters", CommandXhci);
    gConsole.Register2("show", "input", "input counters (xhci or virtio)", CommandInputDiag);
    gConsole.Register2("show", "ehci", "EHCI CCS / ehci hid retry", CommandEhci);
    gConsole.Register2("show", "uhci", "UHCI CCS (PR-H-uhci-1)", CommandUhci);
    gConsole.Register2("show", "ps2", "PS/2 Kbd+Aux (Ps2 Retry)", CommandPs2);
    gConsole.Register("msc",
                    "USB MSC: scan|claim|…|hot(晚插); 桌面不自动 Claim",
                    CommandMsc);
    gConsole.RegisterAliasLine("xhci", "show", "xhci");
    gConsole.RegisterAliasLine("input", "show", "input");
    gConsole.RegisterAliasLine("ehci", "show", "ehci");
    gConsole.RegisterAliasLine("uhci", "show", "uhci");
    gConsole.RegisterAliasLine("ps2", "show", "ps2");
}
