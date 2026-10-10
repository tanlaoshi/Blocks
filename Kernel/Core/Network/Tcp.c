/*
 * Tcp.c — K40：单连接 TCP（对标现网 Tcp legacy 薄）
 *
 * 【初学者】
 * 状态：LISTEN / SYN_SENT / SYN_RCVD / ESTABLISHED。
 * 服务端回显；客户端 connect+send。不做多连接 / 拥塞 / 完备重传（另刀）。
 */
#include "Network.h"
#include "LwIp.h"
#include "HalNet.h"
#include "HalSerial.h"
#include "SerialConfig.h"

#if defined(__x86_64__) || defined(_M_X64)

#define TCP_HDR_LEN 20u
#define TCP_FLAG_FIN 0x01u
#define TCP_FLAG_SYN 0x02u
#define TCP_FLAG_RST 0x04u
#define TCP_FLAG_PSH 0x08u
#define TCP_FLAG_ACK 0x10u
#define TCP_MSS 512u
#define TCP_ADV_WND 4096u
#define TCP_SND_BUF 1024u
#define IP_PROTO_TCP 6u
#define ETH_HDR 14u
#define IP_HDR 20u

static NETWORK_TCP_STATE gState;
static UINT16 gLocalPort;
static UINT16 gPeerPort;
static UINT32 gPeerIp;
static UINT16 gIss = 1000u;
static UINT32 gSndUna;
static UINT32 gSndNxt;
static UINT32 gRcvNxt;
static UINT16 gPeerWnd = TCP_ADV_WND;
static int gClientMode;
static UINT8 gSndBuf[TCP_SND_BUF];
static UINT32 gSndBufLen;
static UINT32 gPollTicks;

static void Copy(void *D, const void *S, UINTN N) {
    UINT8 *A = (UINT8 *)D;
    const UINT8 *B = (const UINT8 *)S;
    UINTN i;
    for (i = 0; i < N; i++) {
        A[i] = B[i];
    }
}

static UINT16 Sum16(const UINT8 *P, UINTN Len) {
    UINT32 Sum = 0;
    UINTN i;
    for (i = 0; i + 1 < Len; i += 2) {
        Sum += ((UINT32)P[i] << 8) | (UINT32)P[i + 1];
    }
    if (i < Len) {
        Sum += (UINT32)P[i] << 8;
    }
    while (Sum >> 16) {
        Sum = (Sum & 0xFFFFu) + (Sum >> 16);
    }
    return (UINT16)(~Sum);
}

static UINT16 TcpCsum(UINT32 SrcIp, UINT32 DstIp, const UINT8 *Seg, UINTN SegLen) {
    UINT8 Pseudo[12 + TCP_HDR_LEN + TCP_MSS];
    UINTN i;

    if (SegLen + 12u > sizeof(Pseudo)) {
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
    Pseudo[9] = IP_PROTO_TCP;
    Pseudo[10] = (UINT8)(SegLen >> 8);
    Pseudo[11] = (UINT8)SegLen;
    for (i = 0; i < SegLen; i++) {
        Pseudo[12 + i] = Seg[i];
    }
    /* 主机序校验和；写入头字段时再拆成大端字节 */
    return Sum16(Pseudo, 12 + SegLen);
}

static void PutHex16(UINT16 V) {
    HalSerialWriteChannelHex32(SLOG_NET, (UINT32)V);
}

void TcpInitialize(void) {
    gState = NETWORK_TCP_CLOSED;
    gLocalPort = 0;
    gPeerPort = 0;
    gPeerIp = 0;
    gSndUna = 0;
    gSndNxt = 0;
    gRcvNxt = 0;
    gPeerWnd = TCP_ADV_WND;
    gClientMode = 0;
    gSndBufLen = 0;
    gPollTicks = 0;
}

NETWORK_TCP_STATE TcpGetState(void) {
    return gState;
}

static int SendSeg(UINT8 Flags, const void *Data, UINTN Len, UINT32 Seq, UINT32 Ack) {
    UINT8 Buf[TCP_HDR_LEN + TCP_MSS];
    UINTN i;

    if (Len > TCP_MSS || !HalNetReady()) {
        return -1;
    }
    /* 端口/序号/ACK 按大端写入（主机序数值直接拆字节） */
    Buf[0] = (UINT8)(gLocalPort >> 8);
    Buf[1] = (UINT8)(gLocalPort & 0xFFu);
    Buf[2] = (UINT8)(gPeerPort >> 8);
    Buf[3] = (UINT8)(gPeerPort & 0xFFu);
    Buf[4] = (UINT8)(Seq >> 24);
    Buf[5] = (UINT8)(Seq >> 16);
    Buf[6] = (UINT8)(Seq >> 8);
    Buf[7] = (UINT8)Seq;
    Buf[8] = (UINT8)(Ack >> 24);
    Buf[9] = (UINT8)(Ack >> 16);
    Buf[10] = (UINT8)(Ack >> 8);
    Buf[11] = (UINT8)Ack;
    Buf[12] = (UINT8)((TCP_HDR_LEN / 4u) << 4);
    Buf[13] = Flags;
    Buf[14] = (UINT8)(TCP_ADV_WND >> 8);
    Buf[15] = (UINT8)(TCP_ADV_WND & 0xFFu);
    Buf[16] = 0;
    Buf[17] = 0;
    Buf[18] = 0;
    Buf[19] = 0;
    for (i = 0; i < Len; i++) {
        Buf[TCP_HDR_LEN + i] = ((const UINT8 *)Data)[i];
    }
    {
        UINT16 C = TcpCsum(NetworkSelfIp(), gPeerIp, Buf, TCP_HDR_LEN + Len);
        Buf[16] = (UINT8)(C >> 8);
        Buf[17] = (UINT8)(C & 0xFFu);
    }
    return NetworkSendIp(gPeerIp, IP_PROTO_TCP, Buf, TCP_HDR_LEN + Len);
}

int TcpListen(UINT16 Port) {
    TcpInitialize();
    gLocalPort = Port;
    gState = NETWORK_TCP_LISTEN;
    HalSerialWriteChannel(SLOG_NET, "tcp: listen ");
    PutHex16(Port);
    HalSerialWriteChannel(SLOG_NET, "\n");
    return 0;
}

void TcpClose(void) {
    if (gState == NETWORK_TCP_ESTABLISHED) {
        (void)SendSeg(TCP_FLAG_FIN | TCP_FLAG_ACK, 0, 0, gSndNxt, gRcvNxt);
        gSndNxt++;
    }
    TcpInitialize();
}

int TcpConnect(UINT32 DstIp, UINT16 DstPort) {
    TcpInitialize();
    gClientMode = 1;
    gLocalPort = (UINT16)(40000u + (gIss & 0xFFu));
    gPeerIp = DstIp;
    gPeerPort = DstPort;
    gSndUna = gIss;
    gSndNxt = gIss;
    gState = NETWORK_TCP_SYN_SENT;
    if (SendSeg(TCP_FLAG_SYN, 0, 0, gSndNxt, 0) != 0) {
        gState = NETWORK_TCP_CLOSED;
        return -1;
    }
    gSndNxt = gIss + 1u;
    return 0;
}

int TcpSend(const void *Data, UINTN Len) {
    if (gState != NETWORK_TCP_ESTABLISHED || Data == 0 || Len == 0) {
        return -1;
    }
    if (gSndBufLen + Len > TCP_SND_BUF) {
        return -1;
    }
    Copy(gSndBuf + gSndBufLen, Data, Len);
    gSndBufLen += (UINT32)Len;
    while (gSndNxt < gSndUna + gSndBufLen) {
        UINT32 Off = gSndNxt - gSndUna;
        UINT32 Unsent = gSndUna + gSndBufLen - gSndNxt;
        UINTN Chunk = TCP_MSS;
        if (Chunk > Unsent) {
            Chunk = Unsent;
        }
        if (Chunk == 0) {
            break;
        }
        if (SendSeg(TCP_FLAG_ACK | TCP_FLAG_PSH, gSndBuf + Off, Chunk, gSndNxt,
                    gRcvNxt) != 0) {
            return -2;
        }
        gSndNxt += (UINT32)Chunk;
    }
    return 0;
}

static void OnListen(UINT32 SrcIp, UINT16 SrcPort, UINT16 DstPort, UINT8 Flags,
                     UINT32 Seq, UINT16 Window) {
    if (DstPort != gLocalPort || (Flags & TCP_FLAG_SYN) == 0) {
        return;
    }
    gPeerIp = SrcIp;
    gPeerPort = SrcPort;
    gRcvNxt = Seq + 1u;
    gSndUna = gIss;
    gSndNxt = gIss;
    gSndBufLen = 0;
    gPeerWnd = (Window != 0) ? Window : 1u;
    gState = NETWORK_TCP_SYN_RCVD;
    if (SendSeg(TCP_FLAG_SYN | TCP_FLAG_ACK, 0, 0, gSndNxt, gRcvNxt) != 0) {
        HalSerialWriteChannel(SLOG_NET, "tcp: syn-ack fail\n");
        gState = NETWORK_TCP_LISTEN;
        return;
    }
    gSndNxt++;
}

static void OnSynSent(UINT32 SrcIp, UINT16 SrcPort, UINT8 Flags, UINT32 Seq,
                      UINT32 Ack, UINT16 Window) {
    if (SrcIp != gPeerIp || SrcPort != gPeerPort) {
        return;
    }
    if ((Flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) != (TCP_FLAG_SYN | TCP_FLAG_ACK)) {
        return;
    }
    gRcvNxt = Seq + 1u;
    gSndUna = Ack;
    gSndNxt = Ack;
    gSndBufLen = 0;
    gPeerWnd = (Window != 0) ? Window : 1u;
    (void)SendSeg(TCP_FLAG_ACK, 0, 0, gSndNxt, gRcvNxt);
    gState = NETWORK_TCP_ESTABLISHED;
    HalSerialWriteShell("tcp: connected\n");
}

static int OnSynRcvd(UINT32 SrcIp, UINT16 SrcPort, UINT8 Flags, UINT32 Ack,
                     UINT16 Window) {
    if (SrcIp != gPeerIp || SrcPort != gPeerPort) {
        return 0;
    }
    if ((Flags & TCP_FLAG_ACK) == 0 || Ack != gSndNxt) {
        return 0;
    }
    gSndUna = Ack;
    gPeerWnd = (Window != 0) ? Window : 1u;
    gState = NETWORK_TCP_ESTABLISHED;
    HalSerialWriteShell("tcp: client connected\n");
    return 1;
}

static void EchoData(UINT32 Seq, const UINT8 *Data, UINTN DataLen) {
    char Line[96];
    int Pos = 0;
    UINTN i;
    const char *Prefix = "tcp echo: ";

    if (DataLen == 0) {
        return;
    }
    if (Seq != gRcvNxt) {
        (void)SendSeg(TCP_FLAG_ACK, 0, 0, gSndNxt, gRcvNxt);
        return;
    }
    gRcvNxt = Seq + (UINT32)DataLen;
    if (!gClientMode) {
        if (SendSeg(TCP_FLAG_ACK | TCP_FLAG_PSH, Data, DataLen, gSndNxt, gRcvNxt) !=
            0) {
            HalSerialWriteShell("tcp: echo send fail\n");
        } else {
            gSndNxt += (UINT32)DataLen;
        }
        while (Prefix[Pos] && Pos < (int)sizeof(Line) - 2) {
            Line[Pos] = Prefix[Pos];
            Pos++;
        }
        for (i = 0; i < DataLen && Pos < (int)sizeof(Line) - 2; i++) {
            char C = (char)Data[i];
            if (C >= 32 && C <= 126) {
                Line[Pos++] = C;
            }
        }
        Line[Pos++] = '\n';
        Line[Pos] = 0;
        HalSerialWriteShell(Line);
    } else {
        (void)SendSeg(TCP_FLAG_ACK, 0, 0, gSndNxt, gRcvNxt);
    }
}

static void OnEstablished(UINT16 DstPort, UINT32 SrcIp, UINT16 SrcPort, UINT8 Flags,
                          UINT32 Seq, UINT32 Ack, UINT16 Window, const UINT8 *Data,
                          UINTN DataLen) {
    if (SrcIp != gPeerIp || SrcPort != gPeerPort || DstPort != gLocalPort) {
        return;
    }
    gPeerWnd = (Window != 0) ? Window : 1u;
    if (Flags & TCP_FLAG_RST) {
        HalSerialWriteChannel(SLOG_NET, "tcp: reset\n");
        TcpInitialize();
        return;
    }
    if (Flags & TCP_FLAG_ACK) {
        if (Ack > gSndUna) {
            UINT32 Acked = Ack - gSndUna;
            if (Acked > gSndBufLen) {
                Acked = gSndBufLen;
            }
            if (Acked < gSndBufLen) {
                UINT32 i;
                for (i = 0; i < gSndBufLen - Acked; i++) {
                    gSndBuf[i] = gSndBuf[Acked + i];
                }
            }
            gSndBufLen -= Acked;
            gSndUna = Ack;
            if (gSndNxt < gSndUna) {
                gSndNxt = gSndUna;
            }
        }
    }
    EchoData(Seq, Data, DataLen);
    if (Flags & TCP_FLAG_FIN) {
        if (Seq + (UINT32)DataLen == gRcvNxt) {
            gRcvNxt++;
            (void)SendSeg(TCP_FLAG_ACK | (gClientMode ? 0 : TCP_FLAG_FIN), 0, 0,
                          gSndNxt, gRcvNxt);
            if (!gClientMode) {
                gSndNxt++;
                HalSerialWriteChannel(SLOG_NET, "tcp: closed\n");
                TcpInitialize();
            }
        }
    }
}

void TcpInput(UINT32 SrcIp, UINT32 DstIp, const UINT8 *Payload, UINTN Len) {
    UINT16 SrcPort;
    UINT16 DstPort;
    UINT8 Flags;
    UINT32 Seq;
    UINT32 Ack;
    UINTN HdrLen;
    UINT16 Window;
    const UINT8 *Data;
    UINTN DataLen;

    (void)DstIp;
    if (Payload == 0 || Len < TCP_HDR_LEN) {
        return;
    }
    SrcPort = ((UINT16)Payload[0] << 8) | (UINT16)Payload[1];
    DstPort = ((UINT16)Payload[2] << 8) | (UINT16)Payload[3];
    Seq = ((UINT32)Payload[4] << 24) | ((UINT32)Payload[5] << 16) |
          ((UINT32)Payload[6] << 8) | (UINT32)Payload[7];
    Ack = ((UINT32)Payload[8] << 24) | ((UINT32)Payload[9] << 16) |
          ((UINT32)Payload[10] << 8) | (UINT32)Payload[11];
    HdrLen = (UINTN)((Payload[12] >> 4) * 4u);
    Flags = Payload[13];
    Window = ((UINT16)Payload[14] << 8) | (UINT16)Payload[15];
    if (HdrLen < TCP_HDR_LEN || HdrLen > Len) {
        return;
    }
    Data = Payload + HdrLen;
    DataLen = Len - HdrLen;

    if (gState == NETWORK_TCP_LISTEN) {
        OnListen(SrcIp, SrcPort, DstPort, Flags, Seq, Window);
        return;
    }
    if (gState == NETWORK_TCP_SYN_SENT) {
        OnSynSent(SrcIp, SrcPort, Flags, Seq, Ack, Window);
        return;
    }
    if (gState == NETWORK_TCP_SYN_RCVD) {
        if (!OnSynRcvd(SrcIp, SrcPort, Flags, Ack, Window)) {
            return;
        }
    } else if (gState != NETWORK_TCP_ESTABLISHED) {
        return;
    }
    OnEstablished(DstPort, SrcIp, SrcPort, Flags, Seq, Ack, Window, Data, DataLen);
}

void TcpInputFrame(const UINT8 *Frame, int Len) {
    UINT8 Ihl;
    UINT8 Proto;
    UINT32 SrcIp;
    UINT32 DstIp;
    UINT16 Tot;
    UINTN PayOff;
    UINTN PayLen;

    if (Frame == 0 || Len < (int)(ETH_HDR + IP_HDR + TCP_HDR_LEN)) {
        return;
    }
    if (Frame[12] != 0x08 || Frame[13] != 0x00) {
        return;
    }
    Ihl = (UINT8)((Frame[ETH_HDR] & 0x0Fu) * 4u);
    if (Ihl < IP_HDR) {
        return;
    }
    Proto = Frame[ETH_HDR + 9];
    if (Proto != IP_PROTO_TCP) {
        return;
    }
    Tot = ((UINT16)Frame[ETH_HDR + 2] << 8) | (UINT16)Frame[ETH_HDR + 3];
    SrcIp = ((UINT32)Frame[ETH_HDR + 12] << 24) | ((UINT32)Frame[ETH_HDR + 13] << 16) |
            ((UINT32)Frame[ETH_HDR + 14] << 8) | (UINT32)Frame[ETH_HDR + 15];
    DstIp = ((UINT32)Frame[ETH_HDR + 16] << 24) | ((UINT32)Frame[ETH_HDR + 17] << 16) |
            ((UINT32)Frame[ETH_HDR + 18] << 8) | (UINT32)Frame[ETH_HDR + 19];
    PayOff = ETH_HDR + Ihl;
    if ((int)PayOff + (int)TCP_HDR_LEN > Len) {
        return;
    }
    PayLen = (Tot > Ihl) ? (UINTN)(Tot - Ihl) : 0;
    if ((int)(PayOff + PayLen) > Len) {
        PayLen = (UINTN)Len - PayOff;
    }
    TcpInput(SrcIp, DstIp, Frame + PayOff, PayLen);
}

void TcpPoll(int TimeoutMs) {
    UINT8 Rx[1518];
    int Spin;
    int RxLen;
    UINT32 Tries;

    gPollTicks++;
    /* lwIP 活跃时 RX 归 Lwip，勿再吃帧 */
    if (LwIpActive()) {
        return;
    }
    Tries = (TimeoutMs > 0) ? (UINT32)(TimeoutMs / 2) : 1u;
    for (Spin = 0; (UINT32)Spin < Tries; Spin++) {
        RxLen = HalNetReceive(Rx, sizeof(Rx));
        if (RxLen > 0) {
            NetworkEatArpFromFrame(Rx, RxLen);
            TcpInputFrame(Rx, RxLen);
            UdpInputFrame(Rx, RxLen);
        }
        __asm__ volatile("pause");
    }
}

#else /* !X64 */

void TcpInitialize(void) {
}
int TcpListen(UINT16 Port) {
    (void)Port;
    return -1;
}
int TcpConnect(UINT32 DstIp, UINT16 DstPort) {
    (void)DstIp;
    (void)DstPort;
    return -1;
}
int TcpSend(const void *Data, UINTN Len) {
    (void)Data;
    (void)Len;
    return -1;
}
void TcpClose(void) {
}
NETWORK_TCP_STATE TcpGetState(void) {
    return NETWORK_TCP_CLOSED;
}
void TcpInput(UINT32 SrcIp, UINT32 DstIp, const UINT8 *Payload, UINTN Len) {
    (void)SrcIp;
    (void)DstIp;
    (void)Payload;
    (void)Len;
}
void TcpInputFrame(const UINT8 *Frame, int Len) {
    (void)Frame;
    (void)Len;
}
void TcpPoll(int TimeoutMs) {
    (void)TimeoutMs;
}

#endif
