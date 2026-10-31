/*
 * KsendText.c — 拼串口行（十进制 / 8 位十六进制 / key=value）
 *
 * 为什么自己拼：协议行要精确到字节（KSEND READY size=… block=… n=…），
 * 而 Shell 的 gConsole.Write 会把行同时喂给帧缓冲，板包也不该依赖 Services。
 * 一律先拼进调用者的栈缓冲，再由 KsendTextFlush 一次写出（\n 由 Serial 翻 CRLF）。
 */
#include "Ksend/Private.h"

static void PutChar(KSEND_TEXT *Text, char Value) {
    if (Text->Length + 1 >= Text->Capacity) {
        return; /* 截断：宁可少几个字符，也不写出界 */
    }
    Text->Buffer[Text->Length] = Value;
    Text->Length++;
    Text->Buffer[Text->Length] = 0;
}

void KsendTextReset(KSEND_TEXT *Text, char *Buffer, int Capacity) {
    Text->Buffer = Buffer;
    Text->Capacity = Capacity;
    Text->Length = 0;
    if (Buffer != 0 && Capacity > 0) {
        Buffer[0] = 0;
    }
}

void KsendTextPut(KSEND_TEXT *Text, const char *String) {
    if (String == 0) {
        return;
    }
    while (*String != 0) {
        PutChar(Text, *String);
        String++;
    }
}

void KsendTextPutUnsigned(KSEND_TEXT *Text, UINT32 Value) {
    char Digits[10];
    int Count = 0;

    if (Value == 0u) {
        PutChar(Text, '0');
        return;
    }
    while (Value != 0u && Count < (int)sizeof(Digits)) {
        Digits[Count] = (char)('0' + (int)(Value % 10u));
        Value /= 10u;
        Count++;
    }
    while (Count > 0) {
        Count--;
        PutChar(Text, Digits[Count]);
    }
}

void KsendTextPutHex(KSEND_TEXT *Text, UINT32 Value) {
    char Letters[] = "0123456789abcdef";
    int Shift;

    /* 固定 8 位：PC 侧按 8 位十六进制比对，短了不能靠猜补零 */
    for (Shift = 28; Shift >= 0; Shift -= 4) {
        PutChar(Text, Letters[(Value >> Shift) & 0xFu]);
    }
}

void KsendTextPutField(KSEND_TEXT *Text, const char *Key, UINT32 Value) {
    PutChar(Text, ' ');
    KsendTextPut(Text, Key);
    PutChar(Text, '=');
    KsendTextPutUnsigned(Text, Value);
}

void KsendTextPutHexField(KSEND_TEXT *Text, const char *Key, UINT32 Value) {
    PutChar(Text, ' ');
    KsendTextPut(Text, Key);
    PutChar(Text, '=');
    KsendTextPutHex(Text, Value);
}

void KsendTextPutSignedField(KSEND_TEXT *Text, const char *Key, int Value) {
    PutChar(Text, ' ');
    KsendTextPut(Text, Key);
    PutChar(Text, '=');
    if (Value < 0) {
        PutChar(Text, '-');
        KsendTextPutUnsigned(Text, (UINT32)(-Value));
    } else {
        KsendTextPutUnsigned(Text, (UINT32)Value);
    }
}

void KsendTextFlush(KSEND_TEXT *Text) {
    KsendTextPut(Text, "\n");
    KsendSerialWrite(Text->Buffer);
}
