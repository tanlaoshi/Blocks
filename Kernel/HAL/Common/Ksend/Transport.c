/*
 * Transport.c — ksend 传输发送侧：串口 / TCP socket 切换与 Put/Flush
 *
 * 协议字节不改；Get/Drain 见 TransportReceive.c。有线与 Wi‑Fi 共用 lwIP
 * 主址（INADDR_ANY listen），不区分介质。
 */
#include "Ksend/TransportPrivate.h"
#include "Hal.h"
#include "LwIp.h"
#include "Scheduler.h"

int gTransportSocket = -1;
int gTransportSendBroken;
int gTransportUserAbort;
UINT8 gReceiveBuffer[TRANSPORT_RECEIVE_BYTES];
UINT32 gReceiveLength;
UINT32 gReceivePosition;
UINT8 gSendBuffer[TRANSPORT_SEND_BYTES];
UINT32 gSendLength;

int TransportTakeCtrlC(UINT8 Value) {
    if (Value != (UINT8)TRANSPORT_CTRL_C) {
        return 0;
    }
    gTransportUserAbort = 1;
    return 1;
}

/* 1=用户 Ctrl-C；发送等待里也要采，否则卡在 flush/sndbuf 时壳仍死 */
int TransportPollUserAbort(void) {
    UINT8 AbortByte;

    if (gTransportUserAbort) {
        return 1;
    }
    /*
     * 协议走串口时 0x03 是块载荷（真机差分块里常见），不能当中止。
     * 仅 TCP serve：人在 COM 上 Ctrl-C 打断（NUC 串口热更 2026-11-03）。
     */
    if (gTransportSocket < 0) {
        return 0;
    }
    if (HalSerialTryGetByte(&AbortByte) != 0 && TransportTakeCtrlC(AbortByte)) {
        gTransportSendBroken = 1;
        gSendLength = 0;
        return 1;
    }
    return 0;
}

int TransportElapsed(UINT32 Start, UINT32 LimitMilliseconds) {
    return ((UINT32)(HalMilliseconds() - Start)) >= LimitMilliseconds;
}

/*
 * 未发完的字节必须留在缓冲里重试。旧逻辑 Got<=0 仍 gSendLength=0，
 * Wi‑Fi 上 tcp_write 暂失败会把 READY/note 整行扔掉，PC 表现为
 * 「TCP 已连上却永远等不到一行」（NUC 2026-11-02）。
 */
void TransportSendFlush(UINT32 RetryMilliseconds) {
    UINT32 Sent;
    UINT32 Start;
    UINT32 Index;
    UINT32 Remain;
    int Got;

    if (gTransportSocket < 0 || gSendLength == 0u) {
        return;
    }
    Sent = 0;
    Start = HalMilliseconds();
    while (Sent < gSendLength) {
        if (TransportPollUserAbort()) {
            return;
        }
        Got = LwIpSocketSend(gTransportSocket, gSendBuffer + Sent,
                             gSendLength - Sent);
        if (Got > 0) {
            Sent += (UINT32)Got;
            Start = HalMilliseconds();
            continue;
        }
        if (TransportElapsed(Start, RetryMilliseconds)) {
            break;
        }
        LwIpService();
        SchedulerIoBreath();
        HalCpuRelax();
    }
    if (Sent >= gSendLength) {
        gSendLength = 0;
        return;
    }
    if (Sent == 0u) {
        return;
    }
    Remain = gSendLength - Sent;
    for (Index = 0; Index < Remain; Index++) {
        gSendBuffer[Index] = gSendBuffer[Sent + Index];
    }
    gSendLength = Remain;
}

/* 1=缓冲已空；0=超时仍有未发完 */
int TransportSendFlushUntilEmpty(UINT32 LimitMilliseconds) {
    UINT32 Start = HalMilliseconds();

    while (gSendLength > 0u) {
        if (TransportPollUserAbort()) {
            return 0;
        }
        TransportSendFlush(TRANSPORT_SEND_RETRY_MILLISECONDS);
        if (gSendLength == 0u) {
            return 1;
        }
        if (TransportElapsed(Start, LimitMilliseconds)) {
            return 0;
        }
        LwIpService();
        SchedulerIoBreath();
        HalCpuRelax();
    }
    return 1;
}

void KsendTransportUseSerial(void) {
    if (gTransportSocket >= 0) {
        (void)TransportSendFlushUntilEmpty(TRANSPORT_DRAIN_EMPTY_MILLISECONDS);
        /*
         * 勿只 Close：TIME_WAIT 占 listen 口 → 再 serve bind fail
         *（Pi 2026-11-08；对表 SO_REUSE + Abort）。
         */
        (void)LwIpSocketAbort(gTransportSocket);
        gTransportSocket = -1;
    }
    gReceiveLength = 0;
    gReceivePosition = 0;
    gSendLength = 0;
    gTransportSendBroken = 0;
    gTransportUserAbort = 0;
}

void KsendTransportUseSocket(int Socket) {
    KsendTransportUseSerial();
    gTransportSocket = Socket;
    gTransportSendBroken = 0;
    gTransportUserAbort = 0;
}

int KsendTransportSendHealthy(void) {
    return gTransportSendBroken ? 0 : 1;
}

int KsendTransportUserAborted(void) {
    return gTransportUserAbort ? 1 : 0;
}

int KsendTransportSocket(void) {
    return gTransportSocket;
}

void KsendTransportPutByte(UINT8 Value) {
    UINT32 Start;

    if (gTransportSocket < 0) {
        HalSerialPutByte(Value);
        return;
    }
    if (gTransportSendBroken) {
        return;
    }
    /*
     * 文本行 / 块 CRC 表合并刷；'A'/'N' 必须即时出（Pi Wi‑Fi 真机
     * 2026-11-02：ACK 攒着 → PC 超时）。整表逐字节一包会在 NUC Wi‑Fi
     * 上只到百余字节（wanted 2964 have 144，2026-11-03）。
     * 缓冲满时等 flush；超过 DRAIN 仍满 → broken 并返回（NUC 2026-11-03
     * 曾死等吞壳键盘）。丢字节由 Transfer 见 !SendHealthy → FAIL。
     * 512 对齐 TcpSocketSend 单次上限，边发边腾窗（忌过大合并塞满 sndbuf）。
     */
    Start = HalMilliseconds();
    for (;;) {
        if (TransportPollUserAbort()) {
            return;
        }
        if (gSendLength < TRANSPORT_SEND_BYTES) {
            gSendBuffer[gSendLength++] = Value;
            if (Value == (UINT8)KSEND_ACK_WRITTEN ||
                Value == (UINT8)KSEND_ACK_RETRY) {
                TransportSendFlush(TRANSPORT_SEND_RETRY_MILLISECONDS);
            }
            return;
        }
        TransportSendFlush(TRANSPORT_SEND_RETRY_MILLISECONDS);
        if (gSendLength < TRANSPORT_SEND_BYTES) {
            continue;
        }
        if (TransportElapsed(Start, TRANSPORT_DRAIN_EMPTY_MILLISECONDS)) {
            gTransportSendBroken = 1;
            gSendLength = 0;
            return;
        }
        LwIpService();
        SchedulerIoBreath();
        HalCpuRelax();
    }
}
