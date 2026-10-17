/*
 * ShellCommandsNetAddr.c — net config / set ip|gw|dns（PR-N-nic-addr）
 */
#include "ShellPrivate.h"
#include "Console.h"
#include "HalDevices.h"
#include "NetConfig.h"

static void PrintDotted(UINT32 Ip) {
    char Buf[16];

    if (Ip == 0) {
        gConsole.Write("unset");
        return;
    }
    HalNetFormatIp(Ip, Buf, (int)sizeof(Buf));
    gConsole.Write(Buf);
}

static void PrintConfig(void) {
    gConsole.Write("ip   ");
    PrintDotted(NetConfigGetIp());
    gConsole.Write("\nmask ");
    PrintDotted(NetConfigGetMask());
    gConsole.Write("\ngw   ");
    PrintDotted(NetConfigGetGw());
    gConsole.Write("\ndns  ");
    PrintDotted(NetConfigGetDns());
    gConsole.Write("\n");
}

/* Argv[0]=config|ip|gw|dns；其余为点分地址 */
static void CommandNetConfig(int Argc, char **Argv) {
    const char *Key;
    const char *Val;
    UINT32 Ip;
    int Rc;

    NetConfigEnsure();
    if (Argc < 2) {
        PrintConfig();
        return;
    }
    Key = Argv[1];
    if (Argc < 3) {
        gConsole.Write("usage: net config [ip|gw|dns|mask] <addr>\n");
        return;
    }
    Val = Argv[2];
    if (HalNetParseIp(Val, &Ip) != 0) {
        gConsole.Write("bad addr\n");
        return;
    }
    if (Key[0] == 'i') {
        Rc = NetConfigSetIp(Ip);
    } else if (Key[0] == 'g') {
        Rc = NetConfigSetGw(Ip);
    } else if (Key[0] == 'd') {
        Rc = NetConfigSetDns(Ip);
    } else if (Key[0] == 'm') {
        Rc = NetConfigSetMask(Ip);
    } else {
        gConsole.Write("usage: net config [ip|gw|dns|mask] <addr>\n");
        return;
    }
    if (Rc != 0) {
        gConsole.Write("net config: apply failed\n");
        return;
    }
    PrintConfig();
}

/* set ip|gw|dns <addr>：Argv[0]=二级词 */
static void CommandSetAddr(int Argc, char **Argv) {
    const char *Key;
    UINT32 Ip;
    int Rc;

    if (Argc < 2) {
        gConsole.Write("usage: set ip|gw|dns <addr>\n");
        return;
    }
    Key = Argv[0];
    if (HalNetParseIp(Argv[1], &Ip) != 0) {
        gConsole.Write("bad addr\n");
        return;
    }
    if (Key[0] == 'i') {
        Rc = NetConfigSetIp(Ip);
    } else if (Key[0] == 'g') {
        Rc = NetConfigSetGw(Ip);
    } else if (Key[0] == 'd') {
        Rc = NetConfigSetDns(Ip);
    } else {
        gConsole.Write("usage: set ip|gw|dns <addr>\n");
        return;
    }
    if (Rc != 0) {
        gConsole.Write("set: apply failed\n");
        return;
    }
    PrintConfig();
}

/* PR-N-i219-note：PCI Intel 网卡 + e1000 Bind 只读；填路线图现场清单 */
static void CommandNetNote(int Argc, char **Argv) {
    (void)Argc;
    (void)Argv;
    HalNetDumpNicNote(gConsole.Write);
}

void ShellCommandsNetAddrRegister(void) {
    gConsole.Register2("net", "config", "show/set ip gw dns mask", CommandNetConfig);
    gConsole.Register2("net", "note", "e1000/I219 field note (read-only)", CommandNetNote);
    gConsole.Register2("set", "ip", "set IPv4 address", CommandSetAddr);
    gConsole.Register2("set", "gw", "set default gateway", CommandSetAddr);
    gConsole.Register2("set", "dns", "set DNS server", CommandSetAddr);
    gConsole.RegisterAliasLine("setip", "set", "ip");
    gConsole.RegisterAliasLine("setgw", "set", "gw");
    gConsole.RegisterAliasLine("setdns", "set", "dns");
}
