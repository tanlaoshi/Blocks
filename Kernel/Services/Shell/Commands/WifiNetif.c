/*
 * WifiNetif.c — wifinet：Duo 的 WiFi→lwIP 网桥探针（dwifi-netif-1）
 *
 * 判据不经 DHCP：拼 ARP request 直走网桥。子命令实现见 WifiNetifActions.c。
 *
 * 对表 Wifi.c：板符号一律 weak。CI 默认 BOARD=virt 无 Duo 实现；
 * 硬链 milk-v-duo-s 符号会让 riscv virt Actions 挂掉。
 */
#include "ShellPrivate.h"
#include "Console.h"
#include "Hal.h"
#include "WifiNetifPrivate.h"

#define WIFI_NETIF_ARG_COMMAND 1
#define WIFI_NETIF_ARG_TARGET  2
#define WIFI_NETIF_DEFAULT_TARGET 0xC0A81F01u /* 192.168.31.1 课网网关 */

static void WifiNetWriteUsage(void) {
    gConsole.Write("wifinet attach | disarm | tx [ip] | pump [ms] | status\n");
}

static const char *WifiNetArg(int Argc, char **Argv, int Index) {
    return (Index < Argc) ? Argv[Index] : 0;
}

void CommandWifiNetif(int Argc, char **Argv) {
    const char *Name = WifiNetArg(Argc, Argv, WIFI_NETIF_ARG_COMMAND);

#if !defined(__riscv)
    (void)Name;
    gConsole.Write("wifinet: only on milk-v-duo-s\n");
    WifiNetWriteUsage();
#else
    if (!WifiNetBoardPresent()) {
        (void)Name;
        gConsole.Write("wifinet: only on milk-v-duo-s (BOARD_HAS_WIFI)\n");
        WifiNetWriteUsage();
        return;
    }
    if (Name == 0) {
        WifiNetWriteUsage();
    } else if (Name[0] == 'a') {
        WifiNetAttach();
    } else if (Name[0] == 't') {
        UINT32 Target = WIFI_NETIF_DEFAULT_TARGET;
        const char *Text = WifiNetArg(Argc, Argv, WIFI_NETIF_ARG_TARGET);
        if (Text != 0 && WifiNetParseIpv4(Text, &Target) != 0) {
            gConsole.Write("wifinet: bad ip\n");
            return;
        }
        WifiNetTransmit(Target);
    } else if (Name[0] == 'd') {
        WifiNetDetach();
    } else if (Name[0] == 'p') {
        UINT32 Milliseconds = 3000u;
        const char *Text = WifiNetArg(Argc, Argv, WIFI_NETIF_ARG_TARGET);
        if (Text != 0 && WifiNetParseInteger(Text, &Milliseconds) != 0) {
            gConsole.Write("wifinet: bad ms\n");
            return;
        }
        WifiNetPump(Milliseconds);
    } else if (Name[0] == 's') {
        gConsole.Write("wifinet: armed=");
        gConsole.WriteHex32((UINT32)BoardWifiLinkOutputIsArmed());
        gConsole.Write(" lwip_out=");
        WifiNetWriteDecimal(BoardWifiLinkOutputCount());
        gConsole.Write(" fail=");
        WifiNetWriteDecimal(BoardWifiLinkOutputFailCount());
        gConsole.Write(" lwip_in=");
        WifiNetWriteDecimal(BoardWifiFeedCount());
        gConsole.Write(" drop=");
        WifiNetWriteDecimal(BoardWifiFeedDropCount());
        /* 诊断计数（netif-4 调试）；板未提供 weak=0 时打印 0 */
        gConsole.Write(" polls=");
        WifiNetWriteDecimal(BoardWifiFeedPolls != 0 ? BoardWifiFeedPolls() : 0u);
        gConsole.Write(" raw=");
        WifiNetWriteDecimal(BoardWifiFeedRawFrames != 0 ? BoardWifiFeedRawFrames() : 0u);
        gConsole.Write(" sinks=");
        WifiNetWriteDecimal(BoardWifiFeedSinkCalls != 0 ? BoardWifiFeedSinkCalls() : 0u);
        WifiNetPrintMac();
        gConsole.Write("\n");
    } else {
        WifiNetWriteUsage();
    }
#endif
}

void ShellCommandsWifiNetifRegister(void) {
    gConsole.Register("wifinet", "wifinet attach | disarm | tx [ip] | pump [ms] | status",
                      CommandWifiNetif);
}
