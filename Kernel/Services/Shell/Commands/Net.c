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
        gConsole.Write("Net: not available (no NIC)\n");
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
        /* Docs/25 netif-5 / Pi 0x15e：稳定标签供串口 grep；超时对表 Pi 网关探针 4s */
        if (LwIpPing(Ip, 4000) == 0) {
            gConsole.Write("reply from ");
            gConsole.Write(Argv[1]);
            gConsole.Write("\n");
            gConsole.Write("ping_ok=1\n");
        } else {
            gConsole.Write("no reply\n");
            gConsole.Write("ping_ok=0\n");
        }
        return;
    }
#else
    {
        int Rc = HalNetPing(Argv[1], 4000);

        if (Rc == 0) {
            gConsole.Write("reply from ");
            gConsole.Write(Argv[1]);
            gConsole.Write("\n");
            gConsole.Write("ping_ok=1\n");
        } else if (Rc == -1) {
            gConsole.Write("arp fail / bad host\n");
            gConsole.Write("ping_ok=0\n");
        } else if (Rc == -2) {
            gConsole.Write("tx fail\n");
            gConsole.Write("ping_ok=0\n");
        } else {
            gConsole.Write("no reply\n");
            gConsole.Write("ping_ok=0\n");
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

/*
 * 本机 IPv4：优先 NetConfig（DHCP/静态，有线或 Wi‑Fi 主口）；
 * 无配置再回落 Hal 当前址。供网络热更：板上 `ip` → PC `ksend.py --host`。
 */
static void CommandIp(int Argc, char **Argv) {
    char IpBuf[20];
    UINT32 Address;
    int Kind;

    (void)Argc;
    (void)Argv;
    if (!HalNetReady()) {
        gConsole.Write("ip none (no NIC)\n");
        return;
    }
    NetConfigEnsure();
    Address = NetConfigGetIp();
    if (Address == 0u) {
        Address = HalNetGetIpAddress();
    }
    if (Address == 0u) {
        gConsole.Write("ip none (no address yet; try dhcp / wifi join)\n");
        return;
    }
    HalNetFormatIp(Address, IpBuf, (int)sizeof(IpBuf));
    gConsole.Write("ip ");
    gConsole.Write(IpBuf);
    Kind = HalNetPrimaryKind();
    if (Kind == 1) {
        gConsole.Write(" via wifi\n");
    } else if (Kind == 0) {
        gConsole.Write(" via wired\n");
    } else {
        gConsole.Write("\n");
    }
}

void ShellCommandsNetRegister(void) {
    gConsole.Register2("show", "network", "network info", CommandNet);
    gConsole.Register("net", "network info; net config …", CommandNet);
    gConsole.RegisterAliasLine("network", "show", "network");
    gConsole.Register("ip", "print local IPv4 (wired or wifi primary)", CommandIp);
    ShellCommandsNetAddressRegister();
    gConsole.Register("ping", "ICMP echo", CommandPing);
#ifdef LWIP_ENABLE
    gConsole.Register("dns", "DNS A / IPv4 literal (PR-N-dns)", CommandDns);
#endif
    ShellCommandsNetUdpRegister();
    ShellCommandsNetTcpRegister();
    ShellCommandsNetLwipRegister();
}
