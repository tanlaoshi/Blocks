/*
 * Network.c — net / lwip / dns / ping
 *
 * 对标现网 ShellCommandsNet*.c / NetAddr / NetLwip：网络配置与诊断命令分文件。
 */
#include "ShellCommand.h"
#include "Network.h"
#include "Configuration.h"
#include "LwIp.h"
#include "HalSerial.h"

static void Put(const char *S) {
    HalSerialWriteShell(S);
}

static int StringsEqual(const char *A, const char *B) {
    if (A == 0 || B == 0) {
        return 0;
    }
    while (*A && *B && *A == *B) {
        A++;
        B++;
    }
    return *A == 0 && *B == 0;
}

static void PutUnsigned32(UINT32 V) {
    char Digits[12];
    int Count = 0;
    int Index;

    if (V == 0) {
        Put("0");
        return;
    }
    while (V != 0 && Count < 11) {
        Digits[Count++] = (char)('0' + (V % 10u));
        V /= 10u;
    }
    for (Index = Count - 1; Index >= 0; Index--) {
        char One[2];
        One[0] = Digits[Index];
        One[1] = 0;
        Put(One);
    }
}

static void PutIp(UINT32 Ip) {
    PutUnsigned32((Ip >> 24) & 0xFFu);
    Put(".");
    PutUnsigned32((Ip >> 16) & 0xFFu);
    Put(".");
    PutUnsigned32((Ip >> 8) & 0xFFu);
    Put(".");
    PutUnsigned32(Ip & 0xFFu);
}

/* lwip on|status */
static void CommandLwip(int Argc, char **Argv) {
    if (Argc < 2) {
        Put("usage: lwip on|status\n");
        return;
    }
    if (StringsEqual(Argv[1], "on")) {
        if (LwIpInitialize() != 0) {
            Put("lwip: init fail\n");
            return;
        }
        return;
    }
    if (StringsEqual(Argv[1], "status")) {
        Put(LwIpActive() ? "lwip: on\n" : "lwip: off (builtin; run lwip on)\n");
        return;
    }
    Put("usage: lwip on|status\n");
}

/* net — 显示/设置 Configuration（ip|mask|gw|dns） */
static void CommandNet(int Argc, char **Argv) {
    UINT32 Ip;

    ConfigurationEnsure();
    if (Argc < 2) {
        Put("net ip=");
        PutIp(ConfigurationGetIp());
        Put(" mask=");
        PutIp(ConfigurationGetMask());
        Put(" gw=");
        PutIp(ConfigurationGetGw());
        Put(" dns=");
        PutIp(ConfigurationGetDns());
        Put("\n");
        return;
    }
    if (Argc == 3 && StringsEqual(Argv[1], "ip")) {
        if (NetworkParseIp(Argv[2], &Ip) != 0) {
            Put("net: bad ip\n");
            return;
        }
        if (ConfigurationSetIp(Ip) != 0) {
            Put("net: apply fail\n");
            return;
        }
        Put("net: ip ok\n");
        return;
    }
    if (Argc == 3 && StringsEqual(Argv[1], "mask")) {
        if (NetworkParseIp(Argv[2], &Ip) != 0) {
            Put("net: bad mask\n");
            return;
        }
        if (ConfigurationSetMask(Ip) != 0) {
            Put("net: apply fail\n");
            return;
        }
        Put("net: mask ok\n");
        return;
    }
    if (Argc == 3 && StringsEqual(Argv[1], "gw")) {
        if (NetworkParseIp(Argv[2], &Ip) != 0) {
            Put("net: bad gw\n");
            return;
        }
        if (ConfigurationSetGw(Ip) != 0) {
            Put("net: apply fail\n");
            return;
        }
        Put("net: gw ok\n");
        return;
    }
    if (Argc == 3 && StringsEqual(Argv[1], "dns")) {
        if (NetworkParseIp(Argv[2], &Ip) != 0) {
            Put("net: bad dns\n");
            return;
        }
        if (ConfigurationSetDns(Ip) != 0) {
            Put("net: apply fail\n");
            return;
        }
        Put("net: dns ok\n");
        return;
    }
    Put("usage: net | net ip|mask|gw|dns <a.b.c.d>\n");
}

/* dns <name|ip> — 字面量或 lwIP A 记录 */
static void CommandDns(int Argc, char **Argv) {
    UINT32 Ip;
    int Rc;

    if (Argc < 2) {
        Put("usage: dns <name|ip>\n");
        return;
    }
    if (!NetworkNicReady()) {
        Put("dns: no nic\n");
        return;
    }
    Rc = LwIpDnsLookup(Argv[1], &Ip, 5000);
    if (Rc == 0) {
        Put("dns: ");
        Put(Argv[1]);
        Put(" -> ");
        PutIp(Ip);
        Put("\n");
        return;
    }
    Put("dns: fail ");
    if (Rc == -2) {
        Put("(lwip)\n");
    } else if (Rc == -3) {
        Put("(query)\n");
    } else if (Rc == -4) {
        Put("(timeout)\n");
    } else if (Rc == -5) {
        Put("(nxdomain)\n");
    } else {
        PutUnsigned32((UINT32)(-Rc));
        Put("\n");
    }
}

/* ping — ICMP echo；默认 10.0.2.2（QEMU 网关）；lwip on 后走 lwIP */
static void CommandPing(int Argc, char **Argv) {
    const char *Host = "10.0.2.2";
    int Rc;

    if (!NetworkNicReady()) {
        Put("ping: no nic\n");
        return;
    }
    if (Argc >= 2) {
        Host = Argv[1];
    }
    Put("ping ");
    Put(Host);
    Put(LwIpActive() ? " (lwIP) ...\n" : " ...\n");
    if (LwIpActive()) {
        UINT32 Ip;
        if (LwIpDnsLookup(Host, &Ip, 5000) != 0) {
            Put("ping: fail (dns/ip)\n");
            return;
        }
        Rc = LwIpPing(Ip, 3000);
        if (Rc == 0) {
            Put("ping: ok\n");
        } else {
            Put("ping: fail (lwIP timeout)\n");
        }
        return;
    }
    Rc = Ping(Host, 3000);
    if (Rc == 0) {
        Put("ping: ok\n");
        return;
    }
    Put("ping: fail ");
    if (Rc == -1) {
        Put("(net down)\n");
    } else if (Rc == -2) {
        Put("(bad ip)\n");
    } else if (Rc == -3) {
        Put("(arp)\n");
    } else if (Rc == -4) {
        Put("(tx)\n");
    } else if (Rc == -5) {
        Put("(timeout)\n");
    } else {
        PutUnsigned32((UINT32)(-Rc));
        Put("\n");
    }
}

void NetworkRegister(void) {
    ShellCommandRegister("ping", "ICMP echo (default 10.0.2.2)", CommandPing);
    ShellCommandRegister("lwip", "lwip on|status", CommandLwip);
    ShellCommandRegister("net", "show/set ip|mask|gw|dns", CommandNet);
    ShellCommandRegister("dns", "resolve name or literal IP", CommandDns);
}
