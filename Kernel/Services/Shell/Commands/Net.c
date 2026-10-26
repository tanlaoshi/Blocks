/*
 * ShellCommandsNet.c — PR-S3-shellnet-1：net / ping / dns + 注册汇总
 *
 * udp/tcp/lwip 见 ShellCommandsNet{Udp,Tcp,Lwip}.c；地址见 NetAddr.c。
 */
#include "ShellPrivate.h"
#include "Console.h"
#include "Hal.h"
#include "LwIp.h"
#include "NetConfig.h"
#include "HalDevices.h"

static void CommandNet(int Argc, char **Argv) {
    char IpBuf[20];
    char Hex[3];
    UINT8 Mac[6];
    int i;
    static const char Digits[] = "0123456789ABCDEF";

    (void)Argc;
    (void)Argv;
    if (!HalNetReady()) {
        gConsole.Write("Net: not available (no virtio-net)\n");
        return;
    }
    NetConfigEnsure();
    HalNetGetMacAddress(Mac);
    HalNetFormatIp(HalNetGetIpAddress(), IpBuf, sizeof(IpBuf));
    gConsole.Write("mac ");
    for (i = 0; i < 6; i++) {
        Hex[0] = Digits[(Mac[i] >> 4) & 0xF];
        Hex[1] = Digits[Mac[i] & 0xF];
        Hex[2] = 0;
        gConsole.Write(Hex);
        if (i < 5) {
            gConsole.Write(":");
        }
    }
    gConsole.Write("\nip  ");
    gConsole.Write(IpBuf);
    gConsole.Write(" mask ");
    HalNetFormatIp(NetConfigGetMask(), IpBuf, sizeof(IpBuf));
    gConsole.Write(IpBuf);
    gConsole.Write("\ngw  ");
    if (NetConfigGetGw() == 0) {
        gConsole.Write("unset");
    } else {
        HalNetFormatIp(NetConfigGetGw(), IpBuf, sizeof(IpBuf));
        gConsole.Write(IpBuf);
    }
    gConsole.Write(" dns ");
    if (NetConfigGetDns() == 0) {
        gConsole.Write("unset");
    } else {
        HalNetFormatIp(NetConfigGetDns(), IpBuf, sizeof(IpBuf));
        gConsole.Write(IpBuf);
    }
    gConsole.Write("\n");
    {
        int Up = 0;
        UINT32 Mbps = 0;
        int Fd = 0;

        if (HalNetGetLinkInfo(&Up, &Mbps, &Fd)) {
            gConsole.Write("link ");
            gConsole.Write(Up ? "up" : "down");
            if (Up) {
                if (Mbps == 1000u) {
                    gConsole.Write(" 1000");
                } else if (Mbps == 100u) {
                    gConsole.Write(" 100");
                } else if (Mbps == 10u) {
                    gConsole.Write(" 10");
                } else {
                    gConsole.Write(" ?");
                }
                gConsole.Write(Fd ? "/FD" : "/HD");
            }
            gConsole.Write("\n");
        }
    }
    {
        UINT32 TxDone = 0;
        UINT32 RxFrames = 0;
        HalNetGetStats(&TxDone, &RxFrames);
        gConsole.Write("stats tx_done=");
        gConsole.WriteHex32(TxDone);
        gConsole.Write(" rx_frames=");
        gConsole.WriteHex32(RxFrames);
        gConsole.Write("\n");
    }
#ifdef LWIP_ENABLE
    gConsole.Write("stack ");
    gConsole.Write(LwIpActive() ? "lwip (RX unified)\n" : "builtin (run lwip on)\n");
#endif
}

static void CommandPing(int Argc, char **Argv) {
    if (Argc < 2) {
        gConsole.Write("usage: ping <ip>\n");
        return;
    }
    if (!HalNetReady()) {
        gConsole.Write("Net: not available (no link / wifi)\n");
        return;
    }
    {
        int Up = 0;
        UINT32 Mbps = 0;
        int Fd = 0;

        if (HalNetGetLinkInfo(&Up, &Mbps, &Fd) && !Up) {
            gConsole.Write("Net: link down\n");
            return;
        }
    }
    gConsole.Write("ping ");
    gConsole.Write(Argv[1]);
    gConsole.Write(" ...\n");
#ifdef LWIP_ENABLE
    {
        UINT32 Ip;

        if (HalNetParseIp(Argv[1], &Ip) != 0) {
            gConsole.Write("bad ip\n");
            return;
        }
        if (!LwIpActive() && LwIpInitialize() != 0) {
            gConsole.Write("lwip: init failed\n");
            return;
        }
        if (LwIpPing(Ip, 3000) == 0) {
            gConsole.Write("reply from ");
            gConsole.Write(Argv[1]);
            gConsole.Write("\n");
        } else {
            gConsole.Write("no reply\n");
        }
        return;
    }
#else
    {
        int Rc = HalNetPing(Argv[1], 3000);

        if (Rc == 0) {
            gConsole.Write("reply from ");
            gConsole.Write(Argv[1]);
            gConsole.Write("\n");
        } else if (Rc == -1) {
            gConsole.Write("arp fail / bad host\n");
        } else if (Rc == -2) {
            gConsole.Write("tx fail\n");
        } else {
            gConsole.Write("no reply\n");
        }
    }
#endif
}

#ifdef LWIP_ENABLE
/* PR-N-dns：dns <name|ip> → A / 字面量 */
static void CommandDns(int Argc, char **Argv) {
    UINT32 Ip;
    UINT32 LiteralIp;
    int Rc;
    int IsLiteral;
    char IpBuf[16];

    if (Argc < 2) {
        gConsole.Write("usage: dns <name|ip>\n");
        return;
    }
    if (!HalNetReady()) {
        gConsole.Write("dns: net not available\n");
        return;
    }
    IsLiteral = (HalNetParseIp(Argv[1], &LiteralIp) == 0);
    if (!LwIpActive()) {
        if (IsLiteral) {
            HalNetFormatIp(LiteralIp, IpBuf, (int)sizeof(IpBuf));
            gConsole.Write(Argv[1]);
            gConsole.Write(" -> ");
            gConsole.Write(IpBuf);
            gConsole.Write(" (literal; lwip on for names)\n");
            return;
        }
        gConsole.Write("dns: run lwip on for name lookup\n");
        return;
    }
    Rc = LwIpDnsLookup(Argv[1], &Ip, 5000);
    if (Rc != 0) {
        gConsole.Write("dns: fail errno=");
        gConsole.WriteHex32((UINT32)(-Rc));
        gConsole.Write("\n");
        return;
    }
    HalNetFormatIp(Ip, IpBuf, (int)sizeof(IpBuf));
    gConsole.Write(Argv[1]);
    gConsole.Write(" -> ");
    gConsole.Write(IpBuf);
    gConsole.Write(IsLiteral ? " (literal)\n" : " (A)\n");
}
#endif

void ShellCommandsNetRegister(void) {
    gConsole.Register2("show", "network", "network info", CommandNet);
    gConsole.Register("net", "network info; net config …", CommandNet);
    gConsole.RegisterAliasLine("network", "show", "network");
    ShellCommandsNetAddrRegister();
    gConsole.Register("ping", "ICMP echo", CommandPing);
#ifdef LWIP_ENABLE
    gConsole.Register("dns", "DNS A / IPv4 literal (PR-N-dns)", CommandDns);
#endif
    ShellCommandsNetUdpRegister();
    ShellCommandsNetTcpRegister();
    ShellCommandsNetLwipRegister();
}
