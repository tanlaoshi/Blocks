/*
 * ShellCommandsNetLwip.c — PR-S3-shellnet-1：lwip on|status|dhcp（从 Net.c 搬家）
 */
#include "ShellPrivate.h"
#include "Console.h"
#include "LwIp.h"
#include "HalDevices.h"

#ifdef TOY_LWIP
static void ShellLwIpPrintStatus(void) {
    gConsole.Write("lwip: on (RX unified; ping/tcp/udp via lwIP)\n");
    gConsole.Write("  tcp listen=");
    gConsole.WriteHex32(LwIpTcpListenPort());
    gConsole.Write(" udp bind=");
    gConsole.WriteHex32(LwIpUdpBoundPort());
    gConsole.Write(LwIpDhcpRunning() ? " dhcp=on\n" : " dhcp=off\n");
}

static void CommandLwIpDhcp(int Argc, char **Argv) {
    int Rc;

    (void)Argc;
    (void)Argv;
    if (!HalNetReady()) {
        gConsole.Write("lwip dhcp: net not available\n");
        return;
    }
    if (LwIpDhcpJobBusy()) {
        gConsole.Write("lwip dhcp: busy\n");
        return;
    }
    /* 先 Hold：防 Worker 抢在 PromptAfterCommand 前打完并再出一次 toyos> */
    gConsole.JobHoldPrompt();
    Rc = LwIpDhcpEnqueue(12000);
    if (Rc == -1) {
        gConsole.Write("lwip dhcp: busy\n");
        gConsole.JobReleasePrompt();
        return;
    }
    if (Rc != 0) {
        gConsole.Write("lwip dhcp: start fail\n");
        gConsole.JobReleasePrompt();
        return;
    }
    gConsole.Write("lwip dhcp: queued\n");
}

static void CommandLwIp(int Argc, char **Argv) {
    const char *Word;

    /* 正统：lwip on|status|dhcp → Argv[0]=二级；旧：lwip on → Argv[1] */
    if (Argc >= 1 && Argv[0][0] == 'o' && Argv[0][1] == 'n' && Argv[0][2] == 0) {
        Word = Argv[0];
    } else if (Argc >= 1 && Argv[0][0] == 's') {
        Word = Argv[0];
    } else if (Argc >= 1 && Argv[0][0] == 'd') {
        Word = Argv[0];
    } else if (Argc >= 2) {
        Word = Argv[1];
    } else {
        gConsole.Write("usage: lwip on|status|dhcp\n");
        return;
    }
    if (Word[0] == 'o' && Word[1] == 'n' && Word[2] == 0) {
        if (LwIpActive()) {
            gConsole.Write("lwip: already on\n");
            return;
        }
        if (LwIpInitialize() != 0) {
            gConsole.Write("lwip: init failed\n");
            return;
        }
        ShellLwIpPrintStatus();
        return;
    }
    if (Word[0] == 'd') {
        CommandLwIpDhcp(Argc, Argv);
        return;
    }
    if (Word[0] == 's') {
        if (!LwIpActive()) {
            gConsole.Write("lwip: off (builtin stack; run lwip on)\n");
            return;
        }
        ShellLwIpPrintStatus();
        return;
    }
    gConsole.Write("usage: lwip on|status|dhcp\n");
}
#endif

void ShellCommandsNetLwipRegister(void) {
#ifdef TOY_LWIP
    gConsole.Register2("lwip", "on", "enable lwIP stack", CommandLwIp);
    gConsole.Register2("lwip", "status", "lwIP status", CommandLwIp);
    gConsole.Register2("lwip", "dhcp", "DHCP; wait IP then prompt", CommandLwIpDhcp);
    gConsole.Register2("net", "dhcp", "DHCP; wait IP then prompt", CommandLwIpDhcp);
#endif
}
