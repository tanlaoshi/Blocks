/*
 * TcpSocketPrivate.h — TcpSocket.c / TcpSocketIo.c 共享
 */
#pragma once

#include "lwip/opt.h"

#if LWIP_TCP

#include "TcpSocket.h"
#include "Ip.h"
#include "Hal.h"
#include "LwIp.h"
#include "Errno.h"
#include "SerialLog.h"
#include "Scheduler.h"
#include "lwip/tcp.h"
#include "lwip/pbuf.h"

#define SOCK_RX_MAX 32768

typedef struct {
    int Used;
    struct tcp_pcb *Pcb;
    volatile int Phase;
    volatile err_t Err;
    UINT8 Rx[SOCK_RX_MAX];
    UINTN RxLen;
    int Pending[TCP_SOCK_PENDING];
    int PendingCount;
    int IsListen;
} TOY_SOCK;

extern TOY_SOCK gSocks[TCP_SOCK_MAX];

int SockNegErrno(err_t Err);
TOY_SOCK *SockGet(int Sock);
int SockAllocSlot(void);
void SockAbortPcb(TOY_SOCK *S);
void SockSetupConnected(TOY_SOCK *S, struct tcp_pcb *Pcb);
void SockErrCb(void *Arg, err_t Err);
err_t SockConnectedCb(void *Arg, struct tcp_pcb *Pcb, err_t Err);

#endif /* LWIP_TCP */
