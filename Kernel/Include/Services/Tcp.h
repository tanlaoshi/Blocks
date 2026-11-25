/*
 * Tcp.h — 单连接演示栈
 *
 * 和 lwIP 并存。lwIP 打开时收包仍走 gLwIpReceive，本栈不收包。
 * 未打开时由 Shell 的 tcp listen / tcp connect 使用。
 * 不再扩展多连接或拥塞控制。
 */
#ifndef TCP_H
#define TCP_H

#include "BootTypes.h"

typedef enum {
    TCP_CLOSED = 0,
    TCP_LISTEN,
    TCP_SYN_SENT,
    TCP_SYN_RECEIVED,
    TCP_ESTABLISHED,
    /* 四次挥手（教学短态；无拥塞/多连接） */
    TCP_FIN_WAIT_1,   /* 已发 FIN，等 ACK */
    TCP_FIN_WAIT_2,   /* FIN 已 ACK，等对端 FIN */
    TCP_CLOSING,      /* 同时关闭：已发 FIN 又收到 FIN */
    TCP_TIME_WAIT,    /* 收到对端 FIN 并 ACK；短 2MSL 后 CLOSED */
    TCP_CLOSE_WAIT,   /* 收到对端 FIN；等本端 TcpClose */
    TCP_LAST_ACK,     /* CLOSE_WAIT 后已发 FIN，等最后 ACK */
} TCP_STATE;

void TcpInitialize(void);
int  TcpListen(UINT16 Port);
void TcpListenStop(void);
int  TcpConnect(UINT32 DstIp, UINT16 DstPort);
int  TcpSend(const void *Data, UINTN Len);
void TcpClose(void);
TCP_STATE TcpStateGet(void);
/* 课堂投影：状态名短串（如 "FIN_WAIT_1"）；未知则 "?" */
const char *TcpStateName(TCP_STATE State);
UINT16 TcpLocalPort(void);
UINT32 TcpPeerIp(void);
UINT16 TcpPeerPort(void);

/* PR-S2：客户端收包（TcpConnect 后）；OutLen 可为 NULL */
int TcpRecv(void *Buf, UINTN Max, UINTN *OutLen);
int TcpPeerClosed(void);
/* 发纯 ACK（窗口更新 / 催促对端重传） */
int TcpAckSend(void);

void TcpWindowStatsGet(UINT32 *SndUna, UINT32 *SndNxt, UINT32 *BufLen,
                       UINT16 *PeerWnd, UINT8 *Retrans);

void TcpSegmentInput(UINT32 SrcIp, UINT32 DstIp, const UINT8 *Payload, UINTN Len);
void TcpPoll(void);
/* Shell 登记之后，入站失败句走这条路径。没登记则不写。 */
void TcpNoteRegister(void (*Write)(const char *Text));

#endif
