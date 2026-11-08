/*
 * TcpSocket.h — lwIP 持久 TCP socket（用户态 SYS_NET_SOCKET 后端）
 */
#ifndef TCP_SOCKET_H
#define TCP_SOCKET_H

#include "BootTypes.h"

#define TCP_SOCK_MAX     8
#define TCP_SOCK_PENDING 2

/* >=0 sock id；-1 失败 */
int TcpSocketCreate(void);
int TcpSocketBind(int Sock, UINT32 Ip, UINT16 Port);
int TcpSocketListen(int Sock, int Backlog);
/* >=0 新连接 sock id；-1 超时/失败 */
int TcpSocketAccept(int Sock, int TimeoutMs);
int TcpSocketConnect(int Sock, UINT32 DstIp, UINT16 DstPort, int TimeoutMs);
int TcpSocketSend(int Sock, const void *Data, UINTN Len);
/* >0 字节；0 暂无数据；-1 错；-2 对端关闭且缓冲空 */
int TcpSocketRecv(int Sock, void *Buf, UINTN Len, int TimeoutMs);
/*
 * 等到未发/未确认段排空（snd_queuelen==0）或超时。
 * ksend READY/表：仅把字节写入 sndbuf 不够，Wi‑Fi 上主机仍可能永远读不到行。
 */
int TcpSocketFlushOut(int Sock, int TimeoutMs);
int TcpSocketClose(int Sock);
/* 立即拆 PCB（无 TIME_WAIT）；HotSend serve 收口后再 listen 用 */
int TcpSocketAbort(int Sock);

#endif
