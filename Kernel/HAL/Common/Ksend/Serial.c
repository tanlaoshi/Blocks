/*
 * Serial.c — 协议字节收发门面（串口或 TCP；见 Transport.c）
 *
 * 走 Transport：可发 0x00/0x0A，不经 WriteShell 的 \\n 改写。
 * 计时走 HalMilliseconds（板差在各 arch Hal 里）。
 */
#include "Ksend/Private.h"
#include "Hal.h"

void KsendSerialPutByte(UINT8 Value) {
    KsendTransportPutByte(Value);
}

void KsendSerialWrite(const char *Text) {
    int MirrorToHardwareSerial;

    if (Text == 0) {
        return;
    }
    /*
     * TCP 热更时人话 note 只走 socket，串口像死机，难判卡在读命令还是发
     * READY。文本行镜像到硬件串口；块 CRC 表仍只走 PutByte，不进镜像。
     */
    MirrorToHardwareSerial = (KsendTransportSocket() >= 0);
    while (*Text != 0) {
        if (*Text == '\n') {
            if (MirrorToHardwareSerial) {
                HalSerialPutByte((UINT8)'\r');
                HalSerialPutByte((UINT8)'\n');
            }
            KsendSerialPutByte((UINT8)'\r');
            KsendSerialPutByte((UINT8)'\n');
        } else {
            if (MirrorToHardwareSerial) {
                HalSerialPutByte((UINT8)(*Text));
            }
            KsendSerialPutByte((UINT8)(*Text));
        }
        Text++;
    }
    if (KsendTransportSocket() >= 0) {
        /* 文本行（READY/note）必须对端可见后再继续 */
        KsendSerialWaitPeerAcknowledge();
    }
}

int KsendSerialGetByte(UINT8 *Out, UINT32 TimeoutMilliseconds) {
    return KsendTransportGetByte(Out, TimeoutMilliseconds);
}

void KsendSerialDrainInput(UINT32 QuietMilliseconds, UINT32 LimitMilliseconds) {
    KsendTransportDrainInput(QuietMilliseconds, LimitMilliseconds);
}

void KsendSerialDrainTransmitter(void) {
    KsendTransportDrainTransmitter();
}

void KsendSerialWaitPeerAcknowledge(void) {
    KsendTransportWaitPeerAcknowledge();
}

UINT32 KsendMilliseconds(void) {
    return HalMilliseconds();
}

void KsendDelayMilliseconds(UINT32 Milliseconds) {
    HalDelayMilliseconds(Milliseconds);
}
