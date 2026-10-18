/*
 * TcpInputSyn.c — LISTEN / SYN_SENT / SYN_RECEIVED（PR-F-tcp-1）
 */
#include "Tcp.h"
#include "TcpPrivate.h"
#include "Console.h"
#include "Debug.h"

void TcpInputListen(UINT32 SrcIp, UINT16 SrcPort, UINT16 DstPort,
                    UINT8 Flags, UINT32 Seq, UINT16 Window) {
    if (DstPort != gTcp.LocalPort || (Flags & TCP_FLAG_SYN) == 0) {
        return;
    }
    gTcp.PeerIp = SrcIp;
    gTcp.PeerPort = SrcPort;
    gTcp.RcvNxt = Seq + 1;
    gTcp.SndUna = gTcp.Iss;
    gTcp.SndNxt = gTcp.Iss;
    gTcp.SndBufLen = 0;
    gTcp.PeerWnd = Window;
    if (gTcp.PeerWnd == 0) {
        gTcp.PeerWnd = 1;
    }
    gTcp.State = TCP_SYN_RECEIVED;
    if (TcpSendSegment(TCP_FLAG_SYN | TCP_FLAG_ACK, 0, 0, gTcp.SndNxt, gTcp.RcvNxt) != 0) {
        gConsole.Write("tcp: syn-ack failed\n");
        gTcp.State = TCP_LISTEN;
        return;
    }
    gTcp.SndNxt++;
}

void TcpInputSynSent(UINT32 SrcIp, UINT16 SrcPort, UINT8 Flags,
                     UINT32 Seq, UINT32 Ack, UINT16 Window) {
    if (SrcIp != gTcp.PeerIp || SrcPort != gTcp.PeerPort) {
        return;
    }
    if ((Flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) == (TCP_FLAG_SYN | TCP_FLAG_ACK)) {
        gTcp.RcvNxt = Seq + 1;
        gTcp.SndUna = Ack;
        gTcp.SndNxt = Ack;
        gTcp.SndBufLen = 0;
        gTcp.PeerWnd = Window;
        if (gTcp.PeerWnd == 0) {
            gTcp.PeerWnd = 1;
        }
        TcpSendSegment(TCP_FLAG_ACK, 0, 0, gTcp.SndNxt, gTcp.RcvNxt);
        gTcp.State = TCP_ESTABLISHED;
        DebugWrite("tcp: connected\n");
        TcpFlushSend();
    }
}

int TcpInputSynRcvd(UINT32 SrcIp, UINT16 SrcPort, UINT8 Flags,
                    UINT32 Ack, UINT16 Window) {
    if (SrcIp != gTcp.PeerIp || SrcPort != gTcp.PeerPort) {
        return 0;
    }
    if ((Flags & TCP_FLAG_ACK) == 0 || Ack != gTcp.SndNxt) {
        return 0;
    }
    gTcp.SndUna = Ack;
    gTcp.PeerWnd = Window;
    if (gTcp.PeerWnd == 0) {
        gTcp.PeerWnd = 1;
    }
    gTcp.State = TCP_ESTABLISHED;
    gConsole.Notify("tcp: client connected\n");
    DebugWrite("tcp: established (echo)\n");
    return 1;
}
