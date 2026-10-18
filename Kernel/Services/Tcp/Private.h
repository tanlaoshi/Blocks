/*
 * Private.h — 单连接 TCP 状态（PR-S-tcp-1）
 */
#ifndef TCP_PRIVATE_H
#define TCP_PRIVATE_H

#include "Tcp.h"

#define TCP_HDR_LEN       20
#define TCP_FLAG_FIN      0x01
#define TCP_FLAG_SYN      0x02
#define TCP_FLAG_RST      0x04
#define TCP_FLAG_PSH      0x08
#define TCP_FLAG_ACK      0x10

#define TCP_SND_BUF       2048
#define TCP_RCV_BUF       32768 /* PR-S2：可缓冲一小 ELF + HTTP 头 */
#define TCP_MSS           512
#define TCP_ADV_WND       4096
#define TCP_CLIENT_ADV_MAX 2048 /* 限流：避免 QEMU 用户网突发打满 RX 环丢包 */
#define TCP_RTO_POLLS     8000
#define TCP_MAX_RETRANS   5

typedef struct {
    UINT16 SrcPort;
    UINT16 DstPort;
    UINT32 Seq;
    UINT32 Ack;
    UINT8  DataOff;
    UINT8  Flags;
    UINT16 Window;
    UINT16 Checksum;
    UINT16 Urgent;
} __attribute__((packed)) TCP_HDR;

typedef struct {
    TCP_STATE State;
    UINT16 LocalPort;
    UINT16 PeerPort;
    UINT32 PeerIp;
    UINT16 Iss;
    UINT32 SndUna;
    UINT32 SndNxt;
    UINT8  SndBuf[TCP_SND_BUF];
    UINT32 SndBufLen;
    UINT16 PeerWnd;
    UINT32 RcvNxt;
    UINT32 PollTicks;
    UINT32 RtoDeadline;
    UINT8  RetransCount;
    int    ClientMode;
    UINT8  RcvBuf[TCP_RCV_BUF];
    UINT32 RcvBufLen;
    UINT32 RcvTotal;
    int    PeerClosed;
} TCP_CONNECTION;

extern TCP_CONNECTION gTcp;

void Note(const char *Text);

static inline UINT16 HostToNet16(UINT16 V) {
    return (UINT16)((V >> 8) | (V << 8));
}

static inline UINT32 HostToNet32(UINT32 V) {
    return ((V & 0xFF) << 24) | ((V & 0xFF00) << 8) |
           ((V >> 8) & 0xFF00) | (V >> 24);
}

static inline UINT16 NetToHost16(UINT16 V) {
    return HostToNet16(V);
}

static inline UINT32 NetToHost32(UINT32 V) {
    return HostToNet32(V);
}

static inline void BufCopy(void *Dst, const void *Src, UINTN Len) {
    UINT8 *D = (UINT8 *)Dst;
    const UINT8 *S = (const UINT8 *)Src;
    UINTN i;

    for (i = 0; i < Len; i++) {
        D[i] = S[i];
    }
}

int SegmentSend(UINT8 Flags, const void *Data, UINTN Len, UINT32 Seq, UINT32 Ack);
void RetransmitArm(void);
void BufferFlush(void);
int BytesQueue(const void *Data, UINTN Len);
void AckProcess(UINT32 Ack);
void Retransmit(void);

/* —— InputSynchronize.c / InputEstablished.c（PR-F-tcp-1） —— */
void Listen(UINT32 SrcIp, UINT16 SrcPort, UINT16 DstPort,
                    UINT8 Flags, UINT32 Seq, UINT16 Window);
void SynchronizeSent(UINT32 SrcIp, UINT16 SrcPort, UINT8 Flags,
                     UINT32 Seq, UINT32 Ack, UINT16 Window);
int SynchronizeReceived(UINT32 SrcIp, UINT16 SrcPort, UINT8 Flags,
                    UINT32 Ack, UINT16 Window);
void Established(UINT16 DstPort, UINT32 SrcIp, UINT16 SrcPort,
                         UINT8 Flags, UINT32 Seq, UINT32 Ack, UINT16 Window,
                         const UINT8 *Data, UINTN DataLen);

#endif
