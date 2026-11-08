/*
 * TcpSocketStub.c — LWIP_TCP 关闭时的空实现
 */
#include "lwip/opt.h"

#if !LWIP_TCP

#include "TcpSocket.h"

int TcpSocketCreate(void) {
    return -1;
}

int TcpSocketBind(int Sock, UINT32 Ip, UINT16 Port) {
    (void)Sock;
    (void)Ip;
    (void)Port;
    return -1;
}

int TcpSocketListen(int Sock, int Backlog) {
    (void)Sock;
    (void)Backlog;
    return -1;
}

int TcpSocketAccept(int Sock, int TimeoutMs) {
    (void)Sock;
    (void)TimeoutMs;
    return -1;
}

int TcpSocketConnect(int Sock, UINT32 DstIp, UINT16 DstPort, int TimeoutMs) {
    (void)Sock;
    (void)DstIp;
    (void)DstPort;
    (void)TimeoutMs;
    return -1;
}

int TcpSocketSend(int Sock, const void *Data, UINTN Len) {
    (void)Sock;
    (void)Data;
    (void)Len;
    return -1;
}

int TcpSocketRecv(int Sock, void *Buf, UINTN Len, int TimeoutMs) {
    (void)Sock;
    (void)Buf;
    (void)Len;
    (void)TimeoutMs;
    return -1;
}

int TcpSocketFlushOut(int Sock, int TimeoutMs) {
    (void)Sock;
    (void)TimeoutMs;
    return -1;
}

int TcpSocketClose(int Sock) {
    (void)Sock;
    return -1;
}

int TcpSocketAbort(int Sock) {
    (void)Sock;
    return -1;
}

#endif /* !LWIP_TCP */
