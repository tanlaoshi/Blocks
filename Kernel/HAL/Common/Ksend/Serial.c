/*
 * Serial.c — 协议用的原始字节收发（含二进制 CRC 表）
 *
 * 走 HalSerialPutByte / TryGetByte：可发 0x00/0x0A，不经 WriteShell 的 \\n 改写。
 * 计时走 HalMilliseconds（板差在各 arch Hal 里）。
 */
#include "Ksend/Private.h"
#include "Hal.h"

static int KsendElapsed(UINT32 Start, UINT32 LimitMilliseconds) {
    return ((UINT32)(HalMilliseconds() - Start)) >= LimitMilliseconds;
}

void KsendSerialPutByte(UINT8 Value) {
    HalSerialPutByte(Value);
}

void KsendSerialWrite(const char *Text) {
    if (Text == 0) {
        return;
    }
    while (*Text != 0) {
        if (*Text == '\n') {
            KsendSerialPutByte((UINT8)'\r');
            KsendSerialPutByte((UINT8)'\n');
        } else {
            KsendSerialPutByte((UINT8)(*Text));
        }
        Text++;
    }
}

int KsendSerialGetByte(UINT8 *Out, UINT32 TimeoutMilliseconds) {
    UINT32 Start = HalMilliseconds();

    for (;;) {
        if (HalSerialTryGetByte(Out) != 0) {
            return 1;
        }
        if (KsendElapsed(Start, TimeoutMilliseconds)) {
            return 0;
        }
        HalCpuRelax();
    }
}

void KsendSerialDrainInput(UINT32 QuietMilliseconds, UINT32 LimitMilliseconds) {
    UINT32 Start = HalMilliseconds();
    UINT32 Last = Start;
    UINT8 Discard;

    for (;;) {
        if (HalSerialTryGetByte(&Discard) != 0) {
            Last = HalMilliseconds();
        } else if (KsendElapsed(Last, QuietMilliseconds) || KsendElapsed(Start, LimitMilliseconds)) {
            return;
        }
    }
}

void KsendSerialDrainTransmitter(void) {
    HalSerialDrainTransmitter();
}

UINT32 KsendMilliseconds(void) {
    return HalMilliseconds();
}

void KsendDelayMilliseconds(UINT32 Milliseconds) {
    HalDelayMilliseconds(Milliseconds);
}
