/*
 * WifiNetifActions.c — wifinet 子命令实现（attach / disarm / tx / pump / status 辅助）
 *
 * 从 WifiNetif.c 拆出，保证单文件 ≤300 行（命名自查铁律）。
 */
#include "ShellPrivate.h"
#include "Console.h"
#include "Hal.h"
#include "WifiNetifPrivate.h"

#if defined(__riscv)

#define WIFI_NETIF_ARP_FRAME     42u
#define WIFI_NETIF_ETHERNET_HDR  14u
#define WIFI_NETIF_IP_LENGTH     4u
#define WIFI_NETIF_ETHERTYPE_ARP 0x0806u
#define WIFI_NETIF_ARP_HTYPE     0x0001u
#define WIFI_NETIF_ARP_PTYPE     0x0800u
#define WIFI_NETIF_ARP_HLEN      6u
#define WIFI_NETIF_ARP_PLEN      4u
#define WIFI_NETIF_ARP_REQUEST   0x0001u

int WifiNetBoardPresent(void) {
    return BoardWifiLinkOutputIsArmed != 0 &&
           BoardWifiArmNetifFromJoin != 0 &&
           BoardWifiTryLinkOutput != 0;
}

int WifiNetParseInteger(const char *Text, UINT32 *Out) {
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

void WifiNetWriteDecimal(UINT32 Value) {
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

static void WifiNetPutBigEndian16(UINT8 *Out, UINT16 Value) {
    Out[0] = (UINT8)(Value >> 8);
    Out[1] = (UINT8)(Value & 0xFFu);
}

int WifiNetParseIpv4(const char *Text, UINT32 *OutIp) {
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

void WifiNetPrintMac(void) {
    UINT8 Mac[WIFI_NETIF_MAC_LENGTH];
    static const char Digits[] = "0123456789abcdef";
    char Text[4];
    UINT32 Index;

    if (BoardWifiCopyStationMacIfArmed(Mac) != 0) {
        gConsole.Write(" mac=(none)");
        return;
    }
    gConsole.Write(" mac=");
    for (Index = 0u; Index < WIFI_NETIF_MAC_LENGTH; Index++) {
        Text[0] = Digits[(Mac[Index] >> 4) & 0xFu];
        Text[1] = Digits[Mac[Index] & 0xFu];
        Text[2] = (Index + 1u < WIFI_NETIF_MAC_LENGTH) ? ':' : 0;
        Text[3] = 0;
        gConsole.Write(Text);
    }
}

void WifiNetAttach(void) {
    if (BoardWifiArmNetifFromJoin() != 0) {
        gConsole.Write("wifinet: no station mac (join first)\n");
        return;
    }
    gConsole.Write("wifinet: attach ok\n");
}

void WifiNetDetach(void) {
    BoardWifiDisarmNetifLinkOutput();
    gConsole.Write("wifinet: disarm requested (see WiFi: netif disarmed)\n");
}

void WifiNetPump(UINT32 Milliseconds) {
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

void WifiNetTransmit(UINT32 TargetIp) {
    UINT8 Frame[WIFI_NETIF_ARP_FRAME];
    UINT8 Station[WIFI_NETIF_MAC_LENGTH];
    UINT32 Index;

    if (BoardWifiLinkOutputIsArmed() == 0) {
        gConsole.Write("wifinet: not armed (run wifinet attach)\n");
        return;
    }
    if (BoardWifiCopyStationMacIfArmed(Station) != 0) {
        gConsole.Write("wifinet: no armed mac\n");
        return;
    }
    for (Index = 0u; Index < WIFI_NETIF_MAC_LENGTH; Index++) {
        Frame[Index] = 0xFFu;
    }
    for (Index = 0u; Index < WIFI_NETIF_MAC_LENGTH; Index++) {
        Frame[WIFI_NETIF_MAC_LENGTH + Index] = Station[Index];
    }
    WifiNetPutBigEndian16(Frame + 12u, (UINT16)WIFI_NETIF_ETHERTYPE_ARP);
    WifiNetPutBigEndian16(Frame + WIFI_NETIF_ETHERNET_HDR + 0u, (UINT16)WIFI_NETIF_ARP_HTYPE);
    WifiNetPutBigEndian16(Frame + WIFI_NETIF_ETHERNET_HDR + 2u, (UINT16)WIFI_NETIF_ARP_PTYPE);
    Frame[WIFI_NETIF_ETHERNET_HDR + 4u] = (UINT8)WIFI_NETIF_ARP_HLEN;
    Frame[WIFI_NETIF_ETHERNET_HDR + 5u] = (UINT8)WIFI_NETIF_ARP_PLEN;
    WifiNetPutBigEndian16(Frame + WIFI_NETIF_ETHERNET_HDR + 6u, (UINT16)WIFI_NETIF_ARP_REQUEST);
    for (Index = 0u; Index < WIFI_NETIF_MAC_LENGTH; Index++) {
        Frame[WIFI_NETIF_ETHERNET_HDR + 8u + Index] = Station[Index];
    }
    for (Index = 0u; Index < WIFI_NETIF_IP_LENGTH; Index++) {
        Frame[WIFI_NETIF_ETHERNET_HDR + 14u + Index] = 0u;
        Frame[WIFI_NETIF_ETHERNET_HDR + 18u + Index] = 0u;
    }
    Frame[WIFI_NETIF_ETHERNET_HDR + 24u] = (UINT8)(TargetIp >> 24);
    Frame[WIFI_NETIF_ETHERNET_HDR + 25u] = (UINT8)((TargetIp >> 16) & 0xFFu);
    Frame[WIFI_NETIF_ETHERNET_HDR + 26u] = (UINT8)((TargetIp >> 8) & 0xFFu);
    Frame[WIFI_NETIF_ETHERNET_HDR + 27u] = (UINT8)(TargetIp & 0xFFu);

    if (BoardWifiTryLinkOutput(Frame, WIFI_NETIF_ARP_FRAME) != 0) {
        gConsole.Write("wifinet: tx FAILED (bridge refused, see counters)\n");
        return;
    }
    gConsole.Write("wifinet: tx ok\n");
}

#endif /* __riscv */
