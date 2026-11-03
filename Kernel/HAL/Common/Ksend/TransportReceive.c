/*
 * TransportReceive.c — ksend 传输接收侧：GetByte / Drain
 */
#include "Ksend/TransportPrivate.h"
#include "Hal.h"
#include "LwIp.h"
#include "Scheduler.h"

int KsendTransportGetByte(UINT8 *Out, UINT32 TimeoutMilliseconds) {
    UINT32 Start;
    int Got;

    if (Out == 0) {
        return 0;
    }
    if (gTransportSocket < 0) {
        Start = HalMilliseconds();
        for (;;) {
            /* 串口即协议线：0x03 是数据，勿 TakeCtrlC（见 TransportPollUserAbort） */
            if (HalSerialTryGetByte(Out) != 0) {
                return 1;
            }
            if (TransportElapsed(Start, TimeoutMilliseconds)) {
                return 0;
            }
            HalCpuRelax();
        }
    }

    /* 先尽量把未发完的 TX 推出去，避免「板已 READY、PC 仍在等行」半开 */
    if (gSendLength > 0u) {
        TransportSendFlush(20u);
    }

    Start = HalMilliseconds();
    for (;;) {
        UINT8 AbortByte;

        /* TCP 热更时串口 Ctrl-C 仍可打断，避免壳/键鼠被 serve 吞死 */
        if (HalSerialTryGetByte(&AbortByte) != 0 && TransportTakeCtrlC(AbortByte)) {
            return 0;
        }
        if (gReceivePosition < gReceiveLength) {
            *Out = gReceiveBuffer[gReceivePosition++];
            return 1;
        }
        gReceivePosition = 0;
        gReceiveLength = 0;
        Got = LwIpSocketRecv(gTransportSocket, gReceiveBuffer,
                             TRANSPORT_RECEIVE_BYTES, 20);
        if (Got > 0) {
            gReceiveLength = (UINT32)Got;
            continue;
        }
        if (Got < 0) {
            return 0;
        }
        if (TransportElapsed(Start, TimeoutMilliseconds)) {
            return 0;
        }
        if (gSendLength > 0u) {
            TransportSendFlush(20u);
        }
        LwIpService();
        SchedulerIoBreath();
        HalCpuRelax();
    }
}

void KsendTransportDrainInput(UINT32 QuietMilliseconds, UINT32 LimitMilliseconds) {
    UINT32 Start = HalMilliseconds();
    UINT32 Last = Start;
    UINT8 Discard;

    for (;;) {
        if (gTransportSocket < 0) {
            if (HalSerialTryGetByte(&Discard) != 0) {
                Last = HalMilliseconds();
                continue;
            }
        } else if (KsendTransportGetByte(&Discard, 1u) != 0) {
            Last = HalMilliseconds();
            continue;
        }
        if (TransportElapsed(Last, QuietMilliseconds) ||
            TransportElapsed(Start, LimitMilliseconds)) {
            return;
        }
        HalCpuRelax();
    }
}

void KsendTransportDrainTransmitter(void) {
    if (gTransportSocket < 0) {
        HalSerialDrainTransmitter();
        return;
    }
    if (gTransportSendBroken) {
        gSendLength = 0;
        return;
    }
    if (TransportSendFlushUntilEmpty(TRANSPORT_DRAIN_EMPTY_MILLISECONDS) == 0) {
        gTransportSendBroken = 1;
        gSendLength = 0;
    }
}

void KsendTransportWaitPeerAcknowledge(void) {
    if (gTransportSocket < 0 || gTransportSendBroken) {
        return;
    }
    /*
     * 应用缓冲空 ≠ 对端已收到。READY/整表末尾必须等 snd_queuelen==0。
     * 表中每 512 B 若也等 ACK，Wi‑Fi 上会 stalled sending block table
     * （主机已见 READY、卡在读表，2026-11-03）。
     */
    KsendTransportDrainTransmitter();
    if (gTransportSendBroken) {
        return;
    }
    if (LwIpSocketFlushOut(gTransportSocket,
                           (int)TRANSPORT_DRAIN_EMPTY_MILLISECONDS) != 0) {
        gTransportSendBroken = 1;
    }
}
