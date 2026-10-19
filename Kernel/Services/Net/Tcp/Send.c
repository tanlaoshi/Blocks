/*
 * Send.c — 校验、发送缓冲、重传（PR-S-tcp-1）
 */
#include "Tcp.h"
#include "Private.h"
#include "HalDevices.h"
#include "Console.h"
#include "Debug.h"

static void BufferConsume(UINT32 Bytes) {
    UINT32 i;

    if (Bytes >= gTcp.SndBufLen) {
        gTcp.SndBufLen = 0;
        return;
    }
    for (i = 0; i < gTcp.SndBufLen - Bytes; i++) {
        gTcp.SndBuf[i] = gTcp.SndBuf[Bytes + i];
    }
    gTcp.SndBufLen -= Bytes;
}

static UINT16 Checksum(UINT32 SrcIp, UINT32 DstIp, const UINT8 *TcpSeg, UINTN TcpLen) {
    UINT8 Pseudo[12 + TCP_HDR_LEN + TCP_MSS];
    UINTN i;

    if (TcpLen + 12 > sizeof(Pseudo)) {
        return 0;
    }
    Pseudo[0] = (UINT8)(SrcIp >> 24);
    Pseudo[1] = (UINT8)(SrcIp >> 16);
    Pseudo[2] = (UINT8)(SrcIp >> 8);
    Pseudo[3] = (UINT8)SrcIp;
    Pseudo[4] = (UINT8)(DstIp >> 24);
    Pseudo[5] = (UINT8)(DstIp >> 16);
    Pseudo[6] = (UINT8)(DstIp >> 8);
    Pseudo[7] = (UINT8)DstIp;
    Pseudo[8] = 0;
    Pseudo[9] = HAL_IP_PROTO_TCP;
    Pseudo[10] = (UINT8)(TcpLen >> 8);
    Pseudo[11] = (UINT8)TcpLen;
    for (i = 0; i < TcpLen; i++) {
        Pseudo[12 + i] = TcpSeg[i];
    }
    return HostToNet16(HalNetChecksum(Pseudo, 12 + TcpLen));
}

static UINT16 WindowAdvertise(void) {
    UINT32 Space;

    if (!gTcp.ClientMode) {
        return TCP_ADV_WND;
    }
    Space = TCP_RCV_BUF - gTcp.RcvBufLen;
    if (Space > TCP_CLIENT_ADV_MAX) {
        Space = TCP_CLIENT_ADV_MAX;
    }
    if (Space > 0xFFFF) {
        Space = 0xFFFF;
    }
    if (Space == 0) {
        return 0;
    }
    return (UINT16)Space;
}

int SegmentSend(UINT8 Flags, const void *Data, UINTN Len, UINT32 Seq, UINT32 Ack) {
    UINT8 Buf[TCP_HDR_LEN + TCP_MSS];
    TCP_HDR *Hdr;
    UINTN i;

    if (Len > TCP_MSS) {
        return -1;
    }
    Hdr = (TCP_HDR *)Buf;
    Hdr->SrcPort = HostToNet16(gTcp.LocalPort);
    Hdr->DstPort = HostToNet16(gTcp.PeerPort);
    Hdr->Seq = HostToNet32(Seq);
    Hdr->Ack = HostToNet32(Ack);
    Hdr->DataOff = (TCP_HDR_LEN / 4) << 4;
    Hdr->Flags = Flags;
    Hdr->Window = HostToNet16(WindowAdvertise());
    Hdr->Checksum = 0;
    Hdr->Urgent = 0;
    for (i = 0; i < Len; i++) {
        Buf[TCP_HDR_LEN + i] = ((const UINT8 *)Data)[i];
    }
    Hdr->Checksum = Checksum(HalNetGetIpAddress(), gTcp.PeerIp, Buf, TCP_HDR_LEN + Len);
    return HalNetSendIp(gTcp.PeerIp, HAL_IP_PROTO_TCP, Buf, TCP_HDR_LEN + Len);
}

void RetransmitArm(void) {
    gTcp.RtoDeadline = gTcp.PollTicks + TCP_RTO_POLLS;
}

void BufferFlush(void) {
    UINT32 Flight;
    UINT32 WinLeft;

    while (gTcp.State == TCP_ESTABLISHED && gTcp.SndNxt < gTcp.SndUna + gTcp.SndBufLen) {
        Flight = gTcp.SndNxt - gTcp.SndUna;
        if (Flight >= gTcp.PeerWnd) {
            break;
        }
        WinLeft = gTcp.PeerWnd - Flight;
        {
            UINT32 Off = gTcp.SndNxt - gTcp.SndUna;
            UINT32 Unsent = gTcp.SndUna + gTcp.SndBufLen - gTcp.SndNxt;
            UINTN Chunk = TCP_MSS;

            if (Chunk > Unsent) {
                Chunk = Unsent;
            }
            if (Chunk > WinLeft) {
                Chunk = WinLeft;
            }
            if (Chunk == 0) {
                break;
            }
            if (SegmentSend(TCP_FLAG_ACK | TCP_FLAG_PSH, gTcp.SndBuf + Off, Chunk,
                               gTcp.SndNxt, gTcp.RcvNxt) != 0) {
                break;
            }
            gTcp.SndNxt += (UINT32)Chunk;
            if (gTcp.SndUna < gTcp.SndNxt) {
                RetransmitArm();
            }
        }
    }
}

int BytesQueue(const void *Data, UINTN Len) {
    if (gTcp.State != TCP_ESTABLISHED || Data == 0 || Len == 0) {
        return -1;
    }
    if (gTcp.SndBufLen + Len > TCP_SND_BUF) {
        return -1;
    }
    BufCopy(gTcp.SndBuf + gTcp.SndBufLen, Data, Len);
    gTcp.SndBufLen += (UINT32)Len;
    return 0;
}

void AckProcess(UINT32 Ack) {
    UINT32 Acked;

    if (Ack <= gTcp.SndUna) {
        return;
    }
    Acked = Ack - gTcp.SndUna;
    if (Acked > gTcp.SndBufLen) {
        Acked = gTcp.SndBufLen;
    }
    BufferConsume(Acked);
    gTcp.SndUna = Ack;
    if (gTcp.SndNxt < gTcp.SndUna) {
        gTcp.SndNxt = gTcp.SndUna;
    }
    if (gTcp.SndBufLen == 0) {
        gTcp.RetransCount = 0;
    } else if (gTcp.SndUna < gTcp.SndNxt) {
        RetransmitArm();
    }
    BufferFlush();
}

void Retransmit(void) {
    UINTN Chunk;
    UINT32 Unacked;

    if (gTcp.State != TCP_ESTABLISHED || gTcp.SndNxt <= gTcp.SndUna) {
        return;
    }
    /* 仅重传 snd_buf 中由 TcpSend 入队的数据；回显为即时发送不在缓冲内 */
    if (gTcp.SndBufLen == 0) {
        gTcp.SndUna = gTcp.SndNxt;
        gTcp.RetransCount = 0;
        return;
    }
    Unacked = gTcp.SndNxt - gTcp.SndUna;
    Chunk = TCP_MSS;
    if (Chunk > Unacked) {
        Chunk = Unacked;
    }
    if (Chunk == 0) {
        return;
    }
    if (SegmentSend(TCP_FLAG_ACK | TCP_FLAG_PSH, gTcp.SndBuf, Chunk, gTcp.SndUna, gTcp.RcvNxt) != 0) {
        return;
    }
    gTcp.RetransCount++;
    if (gTcp.RetransCount >= TCP_MAX_RETRANS) {
        DebugWrite("tcp: retrans limit\n");
        SegmentSend(TCP_FLAG_RST | TCP_FLAG_ACK, 0, 0, gTcp.SndNxt, gTcp.RcvNxt);
        gTcp.State = TCP_CLOSED;
        return;
    }
    gTcp.RtoDeadline = gTcp.PollTicks + TCP_RTO_POLLS * (1U << (gTcp.RetransCount - 1));
}
