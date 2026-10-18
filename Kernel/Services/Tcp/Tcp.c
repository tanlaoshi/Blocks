/*
 * Tcp.c — 自研单连接 TCP（legacy）：握手/回显 + 发送缓冲、对端窗口、超时重传
 *
 * 无慢启动/拥塞控制；RTO 以 ShellTask 轮询次数计（见 TCP_RTO_POLLS）。
 * 生产路径为 lwIP（ThirdParty/README.md）；本文件仅保留教学/无 lwIP 联调。
 */

#include "Tcp.h"
#include "TcpPrivate.h"
#include "Console.h"
#include "Debug.h"

/* 一条教学连接。主动连接收包不回显；RcvTotal 在排空缓冲后仍保留。 */
TCP_CONNECTION gTcp;

void TcpInitialize(void) {
    gTcp.State = TCP_CLOSED;
    gTcp.LocalPort = 0;
    gTcp.PeerPort = 0;
    gTcp.PeerIp = 0;
    gTcp.Iss = 1000;
    gTcp.SndUna = 0;
    gTcp.SndNxt = 0;
    gTcp.SndBufLen = 0;
    gTcp.PeerWnd = TCP_ADV_WND;
    gTcp.RcvNxt = 0;
    gTcp.PollTicks = 0;
    gTcp.RtoDeadline = 0;
    gTcp.RetransCount = 0;
    gTcp.ClientMode = 0;
    gTcp.RcvBufLen = 0;
    gTcp.RcvTotal = 0;
    gTcp.PeerClosed = 0;
}

int TcpListen(UINT16 Port) {
    TcpInitialize();
    gTcp.LocalPort = Port;
    gTcp.State = TCP_LISTEN;
    DebugWrite("tcp: listen ");
    DebugHex32(Port);
    DebugWrite("\n");
    return 0;
}

void TcpListenStop(void) {
    if (gTcp.State == TCP_LISTEN) {
        TcpInitialize();
    }
}

int TcpConnect(UINT32 DstIp, UINT16 DstPort) {
    TcpInitialize();
    gTcp.ClientMode = 1;
    gTcp.LocalPort = (UINT16)(40000 + (gTcp.Iss & 0xFF));
    gTcp.PeerIp = DstIp;
    gTcp.PeerPort = DstPort;
    gTcp.SndUna = gTcp.Iss;
    gTcp.SndNxt = gTcp.Iss;
    gTcp.State = TCP_SYN_SENT;
    if (TcpSendSegment(TCP_FLAG_SYN, 0, 0, gTcp.SndNxt, 0) != 0) {
        gTcp.State = TCP_CLOSED;
        return -1;
    }
    /* SYN 占一序号，便于超时重传 */
    gTcp.SndNxt = gTcp.Iss + 1;
    TcpArmRetrans();
    return 0;
}

int TcpSend(const void *Data, UINTN Len) {
    if (TcpQueueBytes(Data, Len) != 0) {
        return -1;
    }
    TcpFlushSend();
    return 0;
}

void TcpClose(void) {
    if (gTcp.State == TCP_ESTABLISHED) {
        TcpFlushSend();
        TcpSendSegment(TCP_FLAG_FIN | TCP_FLAG_ACK, 0, 0, gTcp.SndNxt, gTcp.RcvNxt);
        gTcp.SndNxt++;
    }
    gTcp.State = TCP_CLOSED;
}

TCP_STATE TcpGetState(void) {
    return gTcp.State;
}

UINT16 TcpLocalPort(void) {
    return gTcp.LocalPort;
}

UINT32 TcpPeerIp(void) {
    return gTcp.PeerIp;
}

UINT16 TcpPeerPort(void) {
    return gTcp.PeerPort;
}

void TcpGetWindowStats(UINT32 *SndUna, UINT32 *SndNxt, UINT32 *BufLen,
                       UINT16 *PeerWnd, UINT8 *Retrans) {
    if (SndUna) {
        *SndUna = gTcp.SndUna;
    }
    if (SndNxt) {
        *SndNxt = gTcp.SndNxt;
    }
    if (BufLen) {
        *BufLen = gTcp.SndBufLen;
    }
    if (PeerWnd) {
        *PeerWnd = gTcp.PeerWnd;
    }
    if (Retrans) {
        *Retrans = gTcp.RetransCount;
    }
}

int TcpRecv(void *Buf, UINTN Max, UINTN *OutLen) {
    UINT32 N;
    UINT32 i;
    UINT8 *D = (UINT8 *)Buf;

    if (OutLen) {
        *OutLen = 0;
    }
    if (!Buf || Max == 0) {
        return -1;
    }
    N = gTcp.RcvBufLen;
    if (N > (UINT32)Max) {
        N = (UINT32)Max;
    }
    for (i = 0; i < N; i++) {
        D[i] = gTcp.RcvBuf[i];
    }
    if (N < gTcp.RcvBufLen) {
        for (i = 0; i < gTcp.RcvBufLen - N; i++) {
            gTcp.RcvBuf[i] = gTcp.RcvBuf[N + i];
        }
    }
    gTcp.RcvBufLen -= N;
    /* 排空后通告窗口，否则对端停在 win=0 */
    if (N > 0 && gTcp.ClientMode && gTcp.State == TCP_ESTABLISHED) {
        TcpSendSegment(TCP_FLAG_ACK, 0, 0, gTcp.SndNxt, gTcp.RcvNxt);
    }
    if (OutLen) {
        *OutLen = N;
    }
    return 0;
}

int TcpPeerClosed(void) {
    return gTcp.PeerClosed;
}

int TcpSendAck(void) {
    if (gTcp.State != TCP_ESTABLISHED && !gTcp.PeerClosed) {
        return -1;
    }
    if (gTcp.State == TCP_CLOSED) {
        return -1;
    }
    return TcpSendSegment(TCP_FLAG_ACK, 0, 0, gTcp.SndNxt, gTcp.RcvNxt);
}

void TcpPoll(void) {
    gTcp.PollTicks++;
    if (gTcp.State == TCP_SYN_SENT) {
        if (gTcp.PollTicks >= gTcp.RtoDeadline) {
            if (gTcp.RetransCount >= TCP_MAX_RETRANS) {
                gTcp.State = TCP_CLOSED;
                return;
            }
            if (TcpSendSegment(TCP_FLAG_SYN, 0, 0, gTcp.Iss, 0) == 0) {
                gTcp.RetransCount++;
                TcpArmRetrans();
            }
        }
        return;
    }
    if (gTcp.State != TCP_ESTABLISHED) {
        return;
    }
    if (gTcp.SndUna < gTcp.SndNxt && gTcp.PollTicks >= gTcp.RtoDeadline) {
        TcpRetransmit();
    }
    TcpFlushSend();
}
