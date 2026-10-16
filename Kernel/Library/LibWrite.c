/*
 * LibWrite.c — Library 文本输出槽（不依赖 Hal / Core）
 *
 * 未注册时丢弃；Core/Serial 或 Console 启动时 LibWriteRegister。
 */
#include "LibWrite.h"
#include "Text.h"

static void (*gLibWrite)(const char *Text);

void LibWriteRegister(void (*Write)(const char *Text)) {
    gLibWrite = Write;
}

void LibWrite(const char *Text) {
    if (!Text || !gLibWrite) {
        return;
    }
    gLibWrite(Text);
}

static void LibWriteHexDigits(UINT64 Value, int Digits) {
    char Buf[17];

    if (Digits < 1) {
        Digits = 1;
    }
    if (Digits > 16) {
        Digits = 16;
    }
    FormatHexadecimal(Buf, (int)sizeof(Buf), Value, Digits, 0, 0);
    LibWrite(Buf);
}

void LibWriteHex32(UINT32 Value) {
    LibWriteHexDigits((UINT64)Value, 8);
}

void LibWriteHex64(UINT64 Value) {
    LibWriteHexDigits(Value, 16);
}
