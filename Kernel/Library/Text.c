/*
 * Text.c — 数字写成文本（从 HAL/X64/PCIe/PCIe.c 原样搬来）
 */
#include "Text.h"

/* 将 UINT32 格式化为静态缓冲区中的 0x 十六进制字符串 */
char *Uint32ToHex(UINT32 Value) {
    static char Buf[16];
    Buf[0] = '0';
    Buf[1] = 'x';
    for (int i = 7; i >= 0; i--) {
        int Digit = (Value >> (i * 4)) & 0xF;
        Buf[9 - i] = (Digit < 10) ? ('0' + Digit) : ('A' + Digit - 10);
    }
    Buf[10] = '\0';
    return Buf;
}

/* 将 UINT8 格式化为十进制字符串 */
char *Uint8ToDecimal(UINT8 Value) {
    static char Buf[8];
    if (Value >= 100) {
        Buf[0] = '0' + Value / 100;
        Buf[1] = '0' + (Value % 100) / 10;
        Buf[2] = '0' + Value % 10;
        Buf[3] = '\0';
    } else if (Value >= 10) {
        Buf[0] = '0' + Value / 10;
        Buf[1] = '0' + Value % 10;
        Buf[2] = '\0';
    } else {
        Buf[0] = '0' + Value;
        Buf[1] = '\0';
    }
    return Buf;
}

/* 将 UINT64 格式化为 0x 十六进制字符串 */
char *Uint64ToHex(UINT64 Value) {
    static char Buf[20];
    Buf[0] = '0';
    Buf[1] = 'x';
    for (int i = 15; i >= 0; i--) {
        int Digit = (Value >> (i * 4)) & 0xF;
        Buf[17 - i] = (Digit < 10) ? ('0' + Digit) : ('A' + Digit - 10);
    }
    Buf[18] = '\0';
    return Buf;
}
