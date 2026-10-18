/*
 * Address.c — IP 文本与查询（PR-S-net-1）
 */
#include "Network/Private.h"
#include "Udp.h"
#include "Tcp.h"
#ifdef TOY_LWIP
#include "Netif.h"
#endif
#include "PhysicalMemory.h"
#include "VirtualMemory.h"
#include "Serial/Serial.h"
#include "Debug.h"
#include "Hal.h"
#include "Driver.h"
#include "DriverNet.h"
#include "DriverNic.h"
#include "Text.h"

void NetFormatIp(UINT32 Ip, char *Buf, int BufLen) {
    if (BufLen < 16) {
        return;
    }
    FormatDottedDecimal(Ip, Buf, BufLen);
}

int NetParseIp(const char *Text, UINT32 *Ip) {
    UINT32 Parts[4];
    int Part = 0;
    UINT32 Val = 0;
    int Digits = 0;

    if (Text == 0 || Ip == 0) {
        return -1;
    }
    while (*Text) {
        if (*Text >= '0' && *Text <= '9') {
            Val = Val * 10 + (UINT32)(*Text - '0');
            if (Val > 255) {
                return -1;
            }
            Digits++;
        } else if (*Text == '.') {
            if (Digits == 0 || Part >= 3) {
                return -1;
            }
            Parts[Part++] = Val;
            Val = 0;
            Digits = 0;
        } else {
            return -1;
        }
        Text++;
    }
    if (Digits == 0 || Part != 3) {
        return -1;
    }
    Parts[3] = Val;
    *Ip = (Parts[0] << 24) | (Parts[1] << 16) | (Parts[2] << 8) | Parts[3];
    return 0;
}

void NetInfo(void) {
    char IpBuf[20];
    if (!gNetOk) {
        DebugWrite("Net: not available\n");
        return;
    }
    NetFormatIp(gIp, IpBuf, sizeof(IpBuf));
    DebugWrite("Net: mac ");
    for (int i = 0; i < 6; i++) {
        DebugWrite(Uint8ToDecimal(gMac[i]));
        if (i < 5) {
            DebugWrite(":");
        }
    }
    DebugWrite(" ip ");
    DebugWrite(IpBuf);
    DebugWrite(" (QEMU user)\n");
}
