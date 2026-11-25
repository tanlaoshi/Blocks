/*
 * InputClose.c — 四次挥手短状态机（FIN_WAIT / CLOSE_WAIT / TIME_WAIT）
 *
 * 主动：EST → FIN_WAIT_1 → FIN_WAIT_2 → TIME_WAIT → CLOSED
 * 被动：EST → CLOSE_WAIT → LAST_ACK → CLOSED
 * 同时：FIN_WAIT_1 → CLOSING → TIME_WAIT → CLOSED
 */
#include "Tcp.h"
#include "Private.h"
#include "Debug.h"

static int PeerMatches(UINT32 SrcIp, UINT16 SrcPort, UINT16 DstPort) {
    return SrcIp == gTcp.PeerIp && SrcPort == gTcp.PeerPort &&
           DstPort == gTcp.LocalPort;
}

/* Ack 是否确认到我们已发出的 FIN（发 FIN 后 SndNxt 已 +1） */
static int FinAcknowledged(UINT32 Ack) {
    return Ack == gTcp.SndNxt;
}

static void EnterTimeWait(void) {
    gTcp.State = TCP_TIME_WAIT;
    gTcp.RtoDeadline = gTcp.PollTicks + TCP_TIME_WAIT_POLLS;
    Note("tcp: TIME_WAIT\n");
    DebugWrite("tcp: TIME_WAIT\n");
}

static void TakePeerFin(UINT32 Seq, UINTN DataLen) {
    if (Seq + (UINT32)DataLen != gTcp.RcvNxt) {
        SegmentSend(TCP_FLAG_ACK, 0, 0, gTcp.SndNxt, gTcp.RcvNxt);
        return;
    }
    gTcp.RcvNxt++;
    gTcp.PeerClosed = 1;
    SegmentSend(TCP_FLAG_ACK, 0, 0, gTcp.SndNxt, gTcp.RcvNxt);
}

/* ESTABLISHED 收到对端 FIN：→ CLOSE_WAIT；回显端立刻发 FIN → LAST_ACK */
void PeerFinFromEstablished(UINT32 Seq, UINTN DataLen) {
    if (Seq + (UINT32)DataLen != gTcp.RcvNxt) {
        SegmentSend(TCP_FLAG_ACK, 0, 0, gTcp.SndNxt, gTcp.RcvNxt);
        return;
    }
    gTcp.RcvNxt++;
    gTcp.PeerClosed = 1;
    gTcp.State = TCP_CLOSE_WAIT;
    Note("tcp: CLOSE_WAIT\n");
    DebugWrite("tcp: CLOSE_WAIT\n");
    SegmentSend(TCP_FLAG_ACK, 0, 0, gTcp.SndNxt, gTcp.RcvNxt);

    if (!gTcp.ClientMode) {
        /* 回显服务：应用侧立刻关 → LAST_ACK */
        SegmentSend(TCP_FLAG_FIN | TCP_FLAG_ACK, 0, 0, gTcp.SndNxt, gTcp.RcvNxt);
        gTcp.SndNxt++;
        gTcp.State = TCP_LAST_ACK;
        Note("tcp: LAST_ACK\n");
        DebugWrite("tcp: LAST_ACK\n");
        RetransmitArm();
    }
}

void CloseSegmentInput(UINT16 DstPort, UINT32 SrcIp, UINT16 SrcPort,
                       UINT8 Flags, UINT32 Seq, UINT32 Ack, UINT16 Window,
                       UINTN DataLen) {
    if (!PeerMatches(SrcIp, SrcPort, DstPort)) {
        return;
    }
    gTcp.PeerWnd = Window == 0 ? 1 : Window;

    if (Flags & TCP_FLAG_RST) {
        gTcp.State = TCP_CLOSED;
        Note("tcp: reset (close path)\n");
        DebugWrite("tcp: reset\n");
        return;
    }

    if (Flags & TCP_FLAG_ACK) {
        AckProcess(Ack);
    }

    switch (gTcp.State) {
    case TCP_FIN_WAIT_1:
        if ((Flags & TCP_FLAG_ACK) && FinAcknowledged(Ack)) {
            if (Flags & TCP_FLAG_FIN) {
                TakePeerFin(Seq, DataLen);
                EnterTimeWait();
            } else {
                gTcp.State = TCP_FIN_WAIT_2;
                Note("tcp: FIN_WAIT_2\n");
                DebugWrite("tcp: FIN_WAIT_2\n");
            }
            break;
        }
        if (Flags & TCP_FLAG_FIN) {
            TakePeerFin(Seq, DataLen);
            gTcp.State = TCP_CLOSING;
            Note("tcp: CLOSING\n");
            DebugWrite("tcp: CLOSING\n");
        }
        break;

    case TCP_FIN_WAIT_2:
        if (Flags & TCP_FLAG_FIN) {
            TakePeerFin(Seq, DataLen);
            EnterTimeWait();
        }
        break;

    case TCP_CLOSING:
        if ((Flags & TCP_FLAG_ACK) && FinAcknowledged(Ack)) {
            EnterTimeWait();
        }
        break;

    case TCP_CLOSE_WAIT:
        /* 等应用 TcpClose；可继续收 ACK */
        break;

    case TCP_LAST_ACK:
        if ((Flags & TCP_FLAG_ACK) && FinAcknowledged(Ack)) {
            gTcp.State = TCP_CLOSED;
            Note("tcp: CLOSED (LAST_ACK)\n");
            DebugWrite("tcp: closed\n");
        }
        break;

    case TCP_TIME_WAIT:
        if (Flags & TCP_FLAG_FIN) {
            /* 重传的对端 FIN：再 ACK，刷新短 2MSL */
            SegmentSend(TCP_FLAG_ACK, 0, 0, gTcp.SndNxt, gTcp.RcvNxt);
            gTcp.RtoDeadline = gTcp.PollTicks + TCP_TIME_WAIT_POLLS;
        }
        break;

    default:
        break;
    }
}

void ClosePoll(void) {
    if (gTcp.State == TCP_TIME_WAIT && gTcp.PollTicks >= gTcp.RtoDeadline) {
        gTcp.State = TCP_CLOSED;
        Note("tcp: CLOSED (TIME_WAIT done)\n");
        DebugWrite("tcp: closed\n");
        return;
    }
    if ((gTcp.State == TCP_FIN_WAIT_1 || gTcp.State == TCP_LAST_ACK) &&
        gTcp.PollTicks >= gTcp.RtoDeadline) {
        if (gTcp.RetransCount >= TCP_MAX_RETRANS) {
            gTcp.State = TCP_CLOSED;
            Note("tcp: close retrans limit\n");
            return;
        }
        if (SegmentSend(TCP_FLAG_FIN | TCP_FLAG_ACK, 0, 0, gTcp.SndNxt - 1,
                        gTcp.RcvNxt) == 0) {
            gTcp.RetransCount++;
            RetransmitArm();
        }
    }
}

int CloseStateActive(void) {
    return gTcp.State == TCP_FIN_WAIT_1 || gTcp.State == TCP_FIN_WAIT_2 ||
           gTcp.State == TCP_CLOSING || gTcp.State == TCP_TIME_WAIT ||
           gTcp.State == TCP_CLOSE_WAIT || gTcp.State == TCP_LAST_ACK;
}
