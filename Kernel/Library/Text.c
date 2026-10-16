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

void FormatHexadecimal(char *Buffer, int Capacity, UINT64 Value,
                       int Digits, int Uppercase, int WithPrefix) {
    int Need;
    int Pos;
    int i;
    const char *Letters;

    if (Digits < 0) {
        Digits = 0;
    }
    Need = Digits + (WithPrefix ? 2 : 0) + 1;
    if (Buffer == 0 || Capacity < Need) {
        if (Buffer != 0 && Capacity > 0) {
            Buffer[0] = 0;
        }
        return;
    }
    Letters = Uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    Pos = 0;
    if (WithPrefix) {
        Buffer[Pos++] = '0';
        Buffer[Pos++] = 'x';
    }
    for (i = Digits - 1; i >= 0; i--) {
        int Shift = i * 4;
        int Nibble = (Shift >= 64) ? 0 : (int)((Value >> Shift) & 0xFu);
        Buffer[Pos++] = Letters[Nibble];
    }
    Buffer[Pos] = 0;
}

char FormatHexadecimalDigit(UINT32 Value) {
    Value &= 0xFu;
    return (char)(Value < 10u ? ('0' + Value) : ('a' + Value - 10u));
}

void FormatDottedDecimal(UINT32 Value, char *Buffer, int Capacity) {
    int Pos;
    int Part;

    if (Capacity < 16) {
        if (Buffer != 0 && Capacity > 0) {
            Buffer[0] = 0;
        }
        return;
    }
    Pos = 0;
    for (Part = 0; Part < 4; Part++) {
        UINT8 Octet = (UINT8)((Value >> ((3 - Part) * 8)) & 0xFFu);
        char *Digits = Uint8ToDecimal(Octet);
        while (*Digits != 0) {
            Buffer[Pos++] = *Digits++;
        }
        if (Part < 3) {
            Buffer[Pos++] = '.';
        }
    }
    Buffer[Pos] = 0;
}
