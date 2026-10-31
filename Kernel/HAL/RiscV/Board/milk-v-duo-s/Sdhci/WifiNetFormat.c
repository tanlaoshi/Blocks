/*
 * WifiNetFormat.c — PR-36：DHCP/ARP 日志要用的地址/参数格式化（不引入 printf）
 *
 * 用户眼里：`WiFi: dhcp: OK ip=192.168.1.23`、`WiFi: arp: reply 192.168.1.1 is-at aa:bb:…`
 *           里的那些点分十进制和 MAC 文本。
 * 从哪读：AicNetFormatMac（复用扫描层的 MAC 格式化）/ AicNetFormatIpv4（RFC 791 §3.1 的
 *         呈现格式）/ AicNetMarkMac（带前缀直接打串口）。
 * 别改：串口没有 printf，格式化必须自己写；MAC 的 `aa:bb:` 形式与扫描/关联日志保持一致，
 *       否则同一次 join/dhcp 里两个 MAC 长得不一样，现场对不上。
 * 为什么单独一个文件：WifiNetFrame.c 只管线上字节，这里只管给人看的文本（PR-36 拆分）。
 */
#include "WifiNetPrivate.h"

void AicNetFormatMac(const UINT8 *Address, char *Text) {
    AicScanFormatAddress(Address, Text);
}

/* 点分十进制（RFC 791 §3.1 的呈现格式），不引入 printf */
void AicNetFormatIpv4(const UINT8 *Address, char *Text) {
    UINT32 Position = 0;
    UINT32 Index;

    for (Index = 0; Index < AIC_NET_IPV4_LENGTH; Index++) {
        UINT32 Value = (UINT32)Address[Index];

        if (Index > 0u) {
            Text[Position++] = '.';
        }
        if (Value >= 100u) {
            Text[Position++] = (char)('0' + (Value / 100u));
        }
        if (Value >= 10u) {
            Text[Position++] = (char)('0' + ((Value / 10u) % 10u));
        }
        Text[Position++] = (char)('0' + (Value % 10u));
    }
    Text[Position] = 0;
}

void AicNetMarkMac(const char *Tag, const UINT8 *Address) {
    char Text[AIC_SCAN_MAC_TEXT_MAX];

    AicNetFormatMac(Address, Text);
    SdhciMark(Tag);
    SdhciMark(Text);
}
