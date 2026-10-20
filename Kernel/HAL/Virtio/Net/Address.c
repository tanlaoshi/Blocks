/*
 * Address.c — IP 文本格式（PR-S-virtionet-virt-1）
 */
#include "Net.h"
#include "Private.h"
#include "Virtio/Virtio.h"
#include "PhysicalMemory.h"
#include "HalSerial.h"
#include "Hal.h"
#include "Udp.h"
#include "Tcp.h"
#include "Driver.h"
#include "DriverNet.h"
#include "SerialLog.h"
#include "Text.h"
#ifdef LWIP_ENABLE
#include "Netif.h"
#endif

void VirtioNetFormatIp(UINT32 Ip, char *Buf, int BufLen) {
    if (!Buf || BufLen < 16) {
        return;
    }
    FormatDottedDecimal(Ip, Buf, BufLen);
}

int VirtioNetParseIp(const char *Text, UINT32 *Ip) {
    UINT32 Parts[4];
    int Part = 0;
    UINT32 Val = 0;
    int Digits = 0;

    if (Text == 0 || Ip == 0) {
        return -1;
    }
    while (*Text) {
        if (*Text >= '0' && *Text <= '9') {
            Val = Val * 10u + (UINT32)(*Text - '0');
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
