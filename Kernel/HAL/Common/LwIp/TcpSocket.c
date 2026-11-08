/*
 * TcpSocket.c — lwIP 持久 TCP socket（NO_SYS raw API）
 * 支持 connect 客户端与 bind/listen/accept 服务端。
 */
#include "lwip/opt.h"

#if LWIP_TCP

#include "TcpSocketPrivate.h"
#include "TcpSocket.h"
#include "Ip.h"
#include "Hal.h"
#include "LwIp.h"
#include "Errno.h"
#include "SerialLog.h"
#include "Scheduler.h"
#include "lwip/tcp.h"
#include "lwip/ip.h"
#include "lwip/pbuf.h"


/* PR-N-dns：lwIP err_t → -(POSIX errno)；供 CRT 置 errno */
int SockNegErrno(err_t Err) {
    switch (Err) {
    case ERR_OK:
        return 0;
    case ERR_RST:
        return -ECONNREFUSED;
    case ERR_ABRT:
    case ERR_CLSD:
        return -ECONNRESET;
    case ERR_RTE:
        return -ENETUNREACH;
    case ERR_TIMEOUT:
        return -ETIMEDOUT;
    case ERR_CONN:
        return -ENOTCONN;
    case ERR_USE:
        return -EEXIST;
    case ERR_VAL:
    case ERR_ARG:
        return -EINVAL;
    case ERR_MEM:
    case ERR_BUF:
        return -ENOMEM;
    case ERR_WOULDBLOCK:
        return -EAGAIN;
    case ERR_INPROGRESS:
        return -EINPROGRESS;
    default:
        return -EIO;
    }
}

TOY_SOCK gSocks[TCP_SOCK_MAX];

TOY_SOCK *SockGet(int Sock) {
    if (Sock < 0 || Sock >= TCP_SOCK_MAX || !gSocks[Sock].Used) {
        return 0;
    }
    return &gSocks[Sock];
}

int SockAllocSlot(void) {
    int i;

    for (i = 0; i < TCP_SOCK_MAX; i++) {
        if (!gSocks[i].Used) {
            gSocks[i].Used = 1;
            gSocks[i].Pcb = NULL;
            gSocks[i].Phase = 0;
            gSocks[i].Err = ERR_OK;
            gSocks[i].RxLen = 0;
            gSocks[i].PendingCount = 0;
            gSocks[i].IsListen = 0;
            return i;
        }
    }
    return -1;
}

void SockAbortPcb(TOY_SOCK *S) {
    if (S->Pcb != NULL) {
        tcp_arg(S->Pcb, NULL);
        tcp_recv(S->Pcb, NULL);
        tcp_sent(S->Pcb, NULL);
        tcp_err(S->Pcb, NULL);
        tcp_poll(S->Pcb, NULL, 0);
        tcp_accept(S->Pcb, NULL);
        tcp_abort(S->Pcb);
        S->Pcb = NULL;
    }
}

static err_t SockRecvCb(void *Arg, struct tcp_pcb *Pcb, struct pbuf *P, err_t Err) {
    TOY_SOCK *S = (TOY_SOCK *)Arg;
    UINTN Space;
    UINTN Copy;

    if (S == NULL) {
        if (P != NULL) {
            pbuf_free(P);
        }
        return ERR_OK;
    }
    if (Err != ERR_OK) {
        S->Err = Err;
        S->Phase = -1;
        return ERR_OK;
    }
    if (P == NULL) {
        /* 对端 FIN：必须 tcp_close，勿只空 Pcb（否则重传/半开） */
        S->Phase = 2;
        if (tcp_close(Pcb) != ERR_OK) {
            tcp_abort(Pcb);
        }
        S->Pcb = NULL;
        return ERR_OK;
    }
    Space = SOCK_RX_MAX - S->RxLen;
    if (Space == 0) {
        return ERR_MEM;
    }
    Copy = P->tot_len;
    if (Copy > Space) {
        Copy = Space;
    }
    pbuf_copy_partial(P, S->Rx + S->RxLen, (u16_t)Copy, 0);
    S->RxLen += Copy;
    tcp_recved(Pcb, (u16_t)Copy);
    /* 标立即 ACK 并刷出：oneshot 正文已在链上，仍需尽快窗口更新 */
    tcp_set_flags(Pcb, TF_ACK_NOW);
    (void)tcp_output(Pcb);
    if ((UINTN)P->tot_len > Copy) {
        /* 削掉已拷前缀，ERR_MEM 让 lwIP 带着剩余再投（勿 free） */
        if (pbuf_remove_header(P, (u16_t)Copy) != 0) {
            pbuf_free(P);
            return ERR_OK;
        }
        return ERR_MEM;
    }
    pbuf_free(P);
    return ERR_OK;
}

void SockErrCb(void *Arg, err_t Err) {
    TOY_SOCK *S = (TOY_SOCK *)Arg;

    if (S == NULL) {
        return;
    }
    S->Pcb = NULL;
    S->Err = Err;
    S->Phase = -1;
}

err_t SockConnectedCb(void *Arg, struct tcp_pcb *Pcb, err_t Err) {
    TOY_SOCK *S = (TOY_SOCK *)Arg;

    if (S == NULL) {
        return Err;
    }
    if (Err != ERR_OK) {
        S->Err = Err;
        S->Phase = -1;
        S->Pcb = NULL;
        return Err;
    }
    SockSetupConnected(S, Pcb);
    return ERR_OK;
}

void SockSetupConnected(TOY_SOCK *S, struct tcp_pcb *Pcb) {
    S->Pcb = Pcb;
    S->Phase = 1;
    S->IsListen = 0;
    S->RxLen = 0;
    /* 与 listen 同 REUSE，避免 TIME_WAIT 挡再 bind（配 SO_REUSE） */
    ip_set_option(Pcb, SOF_REUSEADDR);
    tcp_arg(Pcb, S);
    tcp_recv(Pcb, SockRecvCb);
    tcp_err(Pcb, SockErrCb);
}

static err_t SockAcceptCb(void *Arg, struct tcp_pcb *NewPcb, err_t Err) {
    TOY_SOCK *Listen = (TOY_SOCK *)Arg;
    int Child;
    TOY_SOCK *Cs;

    if (Listen == NULL || Err != ERR_OK || NewPcb == NULL) {
        if (NewPcb != NULL) {
            tcp_abort(NewPcb);
        }
        return ERR_VAL;
    }
    if (Listen->PendingCount >= TCP_SOCK_PENDING) {
        tcp_abort(NewPcb);
        return ERR_MEM;
    }
    Child = SockAllocSlot();
    if (Child < 0) {
        tcp_abort(NewPcb);
        return ERR_MEM;
    }
    Cs = &gSocks[Child];
    tcp_setprio(NewPcb, TCP_PRIO_MIN);
    SockSetupConnected(Cs, NewPcb);
    Listen->Pending[Listen->PendingCount++] = Child;
    return ERR_OK;
}

int TcpSocketCreate(void) {
    return SockAllocSlot();
}

int TcpSocketBind(int Sock, UINT32 Ip, UINT16 Port) {
    TOY_SOCK *S = SockGet(Sock);
    struct tcp_pcb *Pcb;
    err_t Err;

    if (S == NULL || S->Phase == 1 || S->Phase == 4 || Port == 0) {
        return -EINVAL;
    }
    if (S->Pcb == NULL) {
        Pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
        if (Pcb == NULL) {
            return -ENOMEM;
        }
        S->Pcb = Pcb;
    } else {
        Pcb = S->Pcb;
    }
    /*
     * HotSend serve 反复 listen：无 REUSEADDR 时上轮 TIME_WAIT 会 bind fail
     *（NUC 2026-11-08）。与 httpd 等同 lwIP 惯例。
     */
    ip_set_option(Pcb, SOF_REUSEADDR);
    if (Ip == 0) {
        Err = tcp_bind(Pcb, IP_ANY_TYPE, Port);
    } else {
        ip4_addr_t A4;
        ip_addr_t Addr;

        ToyHostIpToLwIp(Ip, &A4);
        ip_addr_copy_from_ip4(Addr, A4);
        Err = tcp_bind(Pcb, &Addr, Port);
    }
    if (Err != ERR_OK) {
        return SockNegErrno(Err);
    }
    S->Phase = 3;
    return 0;
}

int TcpSocketListen(int Sock, int Backlog) {
    TOY_SOCK *S = SockGet(Sock);
    struct tcp_pcb *ListenPcb;

    (void)Backlog;
    if (S == NULL || S->Pcb == NULL || S->Phase != 3) {
        return -1;
    }
    ListenPcb = tcp_listen_with_backlog(S->Pcb, TCP_SOCK_PENDING);
    if (ListenPcb == NULL) {
        return -1;
    }
    S->Pcb = ListenPcb;
    S->IsListen = 1;
    S->PendingCount = 0;
    S->Phase = 4;
    tcp_arg(ListenPcb, S);
    tcp_accept(ListenPcb, SockAcceptCb);
    return 0;
}


#endif /* LWIP_TCP */
