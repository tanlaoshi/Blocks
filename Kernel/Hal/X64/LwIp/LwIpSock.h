/*
 * LwIpSock.h — K43：lwIP 持久 TCP（用户态 socket 后端；仅客户端）
 */
#ifndef LWIP_SOCK_H
#define LWIP_SOCK_H

#include "BootTypes.h"

#define LWIP_SOCK_MAX 4

int LwIpSockCreate(void);
int LwIpSockConnect(int Sock, UINT32 DstIp, UINT16 DstPort, int TimeoutMs);
int LwIpSockSend(int Sock, const void *Data, UINTN Len);
/* >0 字节；0 超时无数据；-1 错；-2 对端关闭且缓冲空 */
int LwIpSockRecv(int Sock, void *Buf, UINTN Len, int TimeoutMs);
int LwIpSockClose(int Sock);

#endif
