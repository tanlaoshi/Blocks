/*
 * WifiNetif.c — wifinet：Duo 的 WiFi→lwIP 网桥探针（dwifi-netif-1）
 *
 * 为什么需要它：netif-1 的判据是"武装后能发一帧出去且 lwip_out>0"，
 * 但此时还没有租约（DHCP 是 netif-4），壳 ping 依赖 IP —— 所以给一个不经过
 * DHCP、不经过 ARP 缓存的探针：自己拼一帧 ARP request，直接走网桥。
 *
 * 子命令：
 *   wifinet attach        用 join 得到的站点 MAC 武装网桥（等价 netif-3 的自动武装）
 *   wifinet tx [ip]       拼一帧 ARP request 发送（默认目标 192.168.31.1）
 *   wifinet status        打印武装状态与 lwip_out / fail 计数
 *
 * 全文件在 __riscv 下才编：x64/arm64 没有 AIC 与 AicJoinLastIndexes。
 */
#include "ShellPrivate.h"
#include "Console.h"
#include "Hal.h"

#if defined(__riscv)
/*
 * 命令层不带 -I 板目录（INCLUDES_HAL 只作用于 HAL 源），所以用相对路径。
 * 这三个头都在 HAL/RiscV/Board/milk-v-duo-s 下。
 */
#include "../../HAL/RiscV/Board/milk-v-duo-s/WifiNetif.h"
#endif

#define WIFINET_ARG_COMMAND   1
#define WIFINET_ARG_TARGET    2
#define WIFINET_ARP_FRAME     42u
#define WIFINET_ETHERNET_HDR  14u
#define WIFINET_MAC_LEN       6u
#define WIFINET_IP_LEN        4u
#define WIFINET_ETHERTYPE_ARP 0x0806u
#define WIFINET_ARP_HTYPE     0x0001u
#define WIFINET_ARP_PTYPE     0x0800u
#define WIFINET_ARP_HLEN      6u
#define WIFINET_ARP_PLEN      4u
#define WIFINET_ARP_REQUEST   0x0001u
#define WIFINET_DEFAULT_TARGET 0xC0A81F01u /* 192.168.31.1 课网网关 */

#if defined(__riscv)
static int WifiNetParseInteger(const char *Text, UINT32 *Out) {
    UINT32 Value = 0u;
    UINT32 Digits = 0u;

    if (Text == 0 || Out == 0) {
        return -1;
    }
    while (*Text >= '0' && *Text <= '9') {
        Value = Value * 10u + (UINT32)(*Text - '0');
        Digits++;
        Text++;
    }
    if (Digits == 0u || *Text != 0 || Value == 0u) {
        return -1;
    }
    *Out = Value;
    return 0;
}

#endif

static void WifiNetWriteUsage(void) {
    gConsole.Write("wifinet attach | disarm | tx [ip] | pump [ms] | status\n");
}

static const char *WifiNetArg(int Argc, char **Argv, int Index) {
    return (Index < Argc) ? Argv[Index] : 0;
}

#if defined(__riscv)
/* Console 只有 WriteHex32；判据要看人读的十进制，这里补一个最小实现 */
static void WifiNetWriteDecimal(UINT32 Value) {
    char Text[11];
    UINT32 Index = 0u;

    if (Value == 0u) {
        gConsole.Write("0");
        return;
    }
    while (Value != 0u && Index < 10u) {
        Text[Index] = (char)('0' + (Value % 10u));
        Value /= 10u;
        Index++;
    }
    while (Index > 0u) {
        char One[2];
        Index--;
        One[0] = Text[Index];
        One[1] = 0;
        gConsole.Write(One);
    }
}

static void WifiNetPutBe16(UINT8 *Out, UINT16 Value) {
    Out[0] = (UINT8)(Value >> 8);
    Out[1] = (UINT8)(Value & 0xFFu);
}

static int WifiNetParseIpv4(const char *Text, UINT32 *OutIp) {
    UINT32 Value = 0u;
    UINT32 Parts = 0u;
    UINT32 Octet = 0u;
    UINT32 Digits = 0u;

    if (Text == 0 || OutIp == 0) {
        return -1;
    }
    while (*Text != 0) {
        if (*Text >= '0' && *Text <= '9') {
            Octet = Octet * 10u + (UINT32)(*Text - '0');
            Digits++;
            if (Octet > 255u) {
                return -1;
            }
        } else if (*Text == '.') {
            if (Digits == 0u || Parts >= 3u) {
                return -1;
            }
            Value = (Value << 8) | Octet;
            Parts++;
            Octet = 0u;
            Digits = 0u;
        } else {
            return -1;
        }
        Text++;
    }
    if (Digits == 0u || Parts != 3u) {
        return -1;
    }
    Value = (Value << 8) | Octet;
    *OutIp = Value;
    return 0;
}

/* 打印已武装的站点 MAC（netif-3 判据：info 里要看得到 WiFi MAC） */
static void WifiNetPrintMac(void) {
    UINT8 Mac[WIFINET_MAC_LEN];
    static const char Digits[] = "0123456789abcdef";
    char Text[4];
    UINT32 Index;

    if (BoardWifiCopyStationMacIfArmed(Mac) != 0) {
        gConsole.Write(" mac=(none)");
        return;
    }
    gConsole.Write(" mac=");
    for (Index = 0u; Index < WIFINET_MAC_LEN; Index++) {
        Text[0] = Digits[(Mac[Index] >> 4) & 0xFu];
        Text[1] = Digits[Mac[Index] & 0xFu];
        Text[2] = (Index + 1u < WIFINET_MAC_LEN) ? ':' : 0;
        Text[3] = 0;
        gConsole.Write(Text);
    }
}

static void WifiNetAttach(void) {
    if (BoardWifiArmNetifFromJoin() != 0) {
        gConsole.Write("wifinet: no station mac (join first)\n");
        return;
    }
    gConsole.Write("wifinet: attach ok\n");
}

/* detach：解除武装（同时摘掉 RX sink）—— 用来验证"Disarm 后不再喂 lwIP" */
static void WifiNetDetach(void) {
    BoardWifiDisarmNetifLinkOutput();
    gConsole.Write("wifinet: disarm requested (see WiFi: netif disarmed)\n");
}

/* pump：在给定毫秒内反复短喂，用于 netif-2 判据（lwip_in>0） */
static void WifiNetPump(UINT32 Milliseconds) {
    UINT32 Elapsed = 0u;
    UINT32 Fed = 0u;

    if (BoardWifiLinkOutputIsArmed() == 0) {
        gConsole.Write("wifinet: not armed (run wifinet attach)\n");
        return;
    }
    while (Elapsed < Milliseconds) {
        Fed += (UINT32)BoardWifiPollFeedLwIp(8u);
        HalDelayMilliseconds(1u);
        Elapsed++;
    }
    gConsole.Write("wifinet: pump fed=");
    WifiNetWriteDecimal(Fed);
    gConsole.Write(" lwip_in=");
    WifiNetWriteDecimal(BoardWifiFeedCount());
    gConsole.Write("\n");
}

static void WifiNetTransmit(UINT32 TargetIp) {
    UINT8 Frame[WIFINET_ARP_FRAME];
    UINT8 Station[WIFINET_MAC_LEN];
    UINT32 Index;

    if (BoardWifiLinkOutputIsArmed() == 0) {
        gConsole.Write("wifinet: not armed (run wifinet attach)\n");
        return;
    }
    if (BoardWifiCopyStationMacIfArmed(Station) != 0) {
        gConsole.Write("wifinet: no armed mac\n");
        return;
    }
    for (Index = 0u; Index < WIFINET_MAC_LEN; Index++) {
        Frame[Index] = 0xFFu;                      /* dst = broadcast */
    }
    for (Index = 0u; Index < WIFINET_MAC_LEN; Index++) {
        Frame[WIFINET_MAC_LEN + Index] = Station[Index];
    }
    WifiNetPutBe16(Frame + 12u, (UINT16)WIFINET_ETHERTYPE_ARP);
    WifiNetPutBe16(Frame + WIFINET_ETHERNET_HDR + 0u, (UINT16)WIFINET_ARP_HTYPE);
    WifiNetPutBe16(Frame + WIFINET_ETHERNET_HDR + 2u, (UINT16)WIFINET_ARP_PTYPE);
    Frame[WIFINET_ETHERNET_HDR + 4u] = (UINT8)WIFINET_ARP_HLEN;
    Frame[WIFINET_ETHERNET_HDR + 5u] = (UINT8)WIFINET_ARP_PLEN;
    WifiNetPutBe16(Frame + WIFINET_ETHERNET_HDR + 6u, (UINT16)WIFINET_ARP_REQUEST);
    for (Index = 0u; Index < WIFINET_MAC_LEN; Index++) {
        Frame[WIFINET_ETHERNET_HDR + 8u + Index] = Station[Index];
    }
    for (Index = 0u; Index < WIFINET_IP_LEN; Index++) {
        Frame[WIFINET_ETHERNET_HDR + 14u + Index] = 0u;   /* sender ip 0.0.0.0 */
        Frame[WIFINET_ETHERNET_HDR + 18u + Index] = 0u;   /* target mac 全 0 */
    }
    Frame[WIFINET_ETHERNET_HDR + 24u] = (UINT8)(TargetIp >> 24);
    Frame[WIFINET_ETHERNET_HDR + 25u] = (UINT8)((TargetIp >> 16) & 0xFFu);
    Frame[WIFINET_ETHERNET_HDR + 26u] = (UINT8)((TargetIp >> 8) & 0xFFu);
    Frame[WIFINET_ETHERNET_HDR + 27u] = (UINT8)(TargetIp & 0xFFu);

    if (BoardWifiTryLinkOutput(Frame, WIFINET_ARP_FRAME) != 0) {
        gConsole.Write("wifinet: tx FAILED (bridge refused, see counters)\n");
        return;
    }
    gConsole.Write("wifinet: tx ok\n");
}
#endif

void CommandWifiNetif(int Argc, char **Argv) {
    const char *Name = WifiNetArg(Argc, Argv, WIFINET_ARG_COMMAND);

#if !defined(__riscv)
    (void)Name;
    gConsole.Write("wifinet: only on milk-v-duo-s\n");
    WifiNetWriteUsage();
#else
    if (Name == 0) {
        WifiNetWriteUsage();
    } else if (Name[0] == 'a') {
        WifiNetAttach();
    } else if (Name[0] == 't') {
        UINT32 Target = WIFINET_DEFAULT_TARGET;
        const char *Text = WifiNetArg(Argc, Argv, WIFINET_ARG_TARGET);
        if (Text != 0 && WifiNetParseIpv4(Text, &Target) != 0) {
            gConsole.Write("wifinet: bad ip\n");
            return;
        }
        WifiNetTransmit(Target);
    } else if (Name[0] == 'd') {
        WifiNetDetach();
    } else if (Name[0] == 'p') {
        UINT32 Milliseconds = 3000u;
        const char *Text = WifiNetArg(Argc, Argv, WIFINET_ARG_TARGET);
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
        WifiNetPrintMac();
        gConsole.Write("\n");
    } else {
        WifiNetWriteUsage();
    }
#endif
}

void ShellCommandsWifiNetifRegister(void) {
    gConsole.Register("wifinet", "wifinet attach | disarm | tx [ip] | pump [ms] | status", CommandWifiNetif);
}
