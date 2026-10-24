/*
 * TcpSocket.c — lwIP 持久 TCP socket（NO_SYS raw API）
 * 支持 connect 客户端与 bind/listen/accept 服务端。
 */
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

#define SOCK_RX_MAX 32768 /* ≥ 课用 HTTP 包；免拆 pbuf 丢段 */




/* PR-N-dns：lwIP err_t → -(POSIX errno)；供 CRT 置 errno */
static int SockNegErrno(err_t Err) {
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

/* Phase: 0 idle/connecting, 1 connected, 2 peer-closed, 3 bound, 4 listening, -1 error */
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

static TOY_SOCK gSocks[TCP_SOCK_MAX];

static TOY_SOCK *SockGet(int Sock) {
    if (Sock < 0 || Sock >= TCP_SOCK_MAX || !gSocks[Sock].Used) {
        return 0;
    }
    return &gSocks[Sock];
}

static int SockAllocSlot(void) {
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

static void SockAbortPcb(TOY_SOCK *S) {
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

static void SockSetupConnected(TOY_SOCK *S, struct tcp_pcb *Pcb);

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

static void SockErrCb(void *Arg, err_t Err) {
    TOY_SOCK *S = (TOY_SOCK *)Arg;

    if (S == NULL) {
        return;
    }
    S->Pcb = NULL;
    S->Err = Err;
    S->Phase = -1;
}

static err_t SockConnectedCb(void *Arg, struct tcp_pcb *Pcb, err_t Err) {
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

static void SockSetupConnected(TOY_SOCK *S, struct tcp_pcb *Pcb) {
    S->Pcb = Pcb;
    S->Phase = 1;
    S->IsListen = 0;
    S->RxLen = 0;
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

int TcpSocketAccept(int Sock, int TimeoutMs) {
    TOY_SOCK *S = SockGet(Sock);
    int Child;
    int Tries;
    int i;
    int Forever;

    if (S == NULL || S->Phase != 4) {
        return -1;
    }
    Forever = (TimeoutMs <= 0);
    Tries = Forever ? 1 : TimeoutMs;
    while (S->PendingCount == 0 && S->Phase == 4) {
        LwIpService();
        HalCpuHalt();
        if (!Forever) {
            if (--Tries <= 0) {
                break;
            }
        }
    }
    if (S->PendingCount == 0) {
        return -1;
    }
    Child = S->Pending[0];
    for (i = 1; i < S->PendingCount; i++) {
        S->Pending[i - 1] = S->Pending[i];
    }
    S->PendingCount--;
    return Child;
}

int TcpSocketConnect(int Sock, UINT32 DstIp, UINT16 DstPort, int TimeoutMs) {
    TOY_SOCK *S = SockGet(Sock);
    struct tcp_pcb *Pcb;
    ip4_addr_t Remote;
    err_t Err;
    int Tries;
    int Neg;

    if (S == NULL || S->Phase == 1 || S->Phase == 4 || S->IsListen) {
        return -EINVAL;
    }
    {
        LwIpLock();

        if (S->Pcb == NULL) {
            Pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
            if (Pcb == NULL) {
                LwIpUnlock();
                return -ENOMEM;
            }
            S->Pcb = Pcb;
        } else {
            Pcb = S->Pcb;
        }
        S->Phase = 0;
        S->Err = ERR_OK;
        tcp_arg(Pcb, S);
        tcp_err(Pcb, SockErrCb);
        ToyHostIpToLwIp(DstIp, &Remote);
        Err = tcp_connect(Pcb, ip_2_ip4(&Remote), DstPort, SockConnectedCb);
        LwIpUnlock();
    }
    if (Err != ERR_OK) {
        SockAbortPcb(S);
        S->Phase = -1;
        return SockNegErrno(Err);
    }
    /*
     * 先忙泵：ARP 应答 / SYN-ACK 常已在 RX 环，勿每次 hlt 等 NUC≈4ms 定时器。
     * 之后再 Halt 等，避免空转烧 CPU。
     * CoopDrain 下 Current=User 时定时器不切 Gui：必须 IoBreath 泵鼠，否则
     * exec SOCKDEMO 等 8s 会饿死 xHCI（进桌面后鼠死）。
     */
    {
        int Burst = 800;
        while (S->Phase == 0 && Burst-- > 0) {
            LwIpService();
            if ((Burst & 63) == 0) {
                SchedulerIoBreath();
            }
        }
    }
    Tries = TimeoutMs > 0 ? TimeoutMs : 8000;
    while (S->Phase == 0 && Tries-- > 0) {
        LwIpService();
        SchedulerIoBreath();
        HalCpuHalt(); /* 等定时器/网卡；无 halt 会把 TimeoutMs 当空转次数瞬间耗尽 */
    }
    if (S->Phase != 1) {
        LogNet("sock: connect fail phase=");
        LogNetHex32((UINT32)S->Phase);
        LogNet(" err=");
        LogNetHex32((UINT32)(INT32)S->Err);
        if ((INT32)S->Err == -14) {
            LogNet(" (RST: host nc -l -p PORT first?)\n");
        } else {
            LogNet("\n");
        }
        Neg = (S->Phase == 0 && S->Err == ERR_OK)
                  ? -ETIMEDOUT
                  : SockNegErrno(S->Err);
        if (Neg == -EIO && S->Phase == 0) {
            Neg = -ETIMEDOUT;
        }
        SockAbortPcb(S);
        S->Phase = -1;
        return Neg;
    }
    return 0;
}

int TcpSocketSend(int Sock, const void *Data, UINTN Len) {
    TOY_SOCK *S = SockGet(Sock);
    err_t Err;
    UINTN Sent = 0;
    int Tries = 2000;

    if (S == NULL || Data == NULL) {
        return -EINVAL;
    }
    while (Sent < Len && Tries-- > 0) {
        UINTN Chunk;
        u16_t Avail;
        int Phase;

        LwIpLock();
        if (S->Phase != 1 || S->Pcb == NULL) {
            LwIpUnlock();
            return Sent > 0 ? (int)Sent : -ENOTCONN;
        }
        Avail = tcp_sndbuf(S->Pcb);
        if (Avail == 0) {
            LwIpUnlock();
            LwIpService();
            HalCpuHalt();
            continue;
        }
        Chunk = Len - Sent;
        if (Chunk > Avail) {
            Chunk = Avail;
        }
        if (Chunk > 512) {
            Chunk = 512;
        }
        Err = tcp_write(S->Pcb, (const UINT8 *)Data + Sent, (u16_t)Chunk,
                        TCP_WRITE_FLAG_COPY);
        if (Err == ERR_MEM) {
            LwIpUnlock();
            LwIpService();
            HalCpuHalt();
            continue;
        }
        if (Err != ERR_OK) {
            LwIpUnlock();
            return Sent > 0 ? (int)Sent : SockNegErrno(Err);
        }
        (void)tcp_output(S->Pcb);
        Sent += Chunk;
        Phase = S->Phase;
        LwIpUnlock();
        LwIpService();
        if (Phase != 1) {
            break;
        }
    }
    if (Sent > 0) {
        return (int)Sent;
    }
    LwIpLock();
    if (S->Phase != 1) {
        LwIpUnlock();
        return -EPIPE;
    }
    LwIpUnlock();
    return -EAGAIN;
}

int TcpSocketRecv(int Sock, void *Buf, UINTN Len, int TimeoutMs) {
    TOY_SOCK *S = SockGet(Sock);
    UINTN N;
    UINTN i;
    int Tries;
    int ForeverHalt;

    if (S == NULL || Buf == NULL || Len == 0) {
        return -EINVAL;
    }
    /* TimeoutMs < 0：非阻塞；==0：一直等；>0：约 TimeoutMs 次 halt 后返回 0 */
    if (TimeoutMs < 0) {
        ForeverHalt = 0;
        Tries = 1;
    } else if (TimeoutMs == 0) {
        ForeverHalt = 1;
        Tries = 0x7fffffff;
    } else {
        ForeverHalt = 1;
        Tries = TimeoutMs;
    }
    while (Tries-- > 0) {
        LwIpLock();
        if (S->RxLen > 0) {
            N = S->RxLen;
            if (N > Len) {
                N = Len;
            }
            for (i = 0; i < N; i++) {
                ((UINT8 *)Buf)[i] = S->Rx[i];
            }
            for (i = N; i < S->RxLen; i++) {
                S->Rx[i - N] = S->Rx[i];
            }
            S->RxLen -= N;
            LwIpUnlock();
            return (int)N;
        }
        if (S->Phase == 2) {
            LwIpUnlock();
            return -2;
        }
        if (S->Phase < 0) {
            int Neg = SockNegErrno(S->Err == ERR_OK ? ERR_CLSD : S->Err);

            LwIpUnlock();
            return Neg;
        }
        if (S->Phase != 1) {
            LwIpUnlock();
            return 0;
        }
        LwIpUnlock();
        LwIpService();
        if (!ForeverHalt) {
            return 0;
        }
        HalCpuHalt();
    }
    return 0;
}

int TcpSocketClose(int Sock) {
    TOY_SOCK *S = SockGet(Sock);
    int Pending[TCP_SOCK_PENDING];
    int Count;
    int i;

    if (S == NULL) {
        return -1;
    }
    LwIpLock();
    if (S->IsListen) {
        Count = S->PendingCount;
        for (i = 0; i < Count; i++) {
            Pending[i] = S->Pending[i];
        }
        S->PendingCount = 0;
        LwIpUnlock();
        for (i = 0; i < Count; i++) {
            TcpSocketClose(Pending[i]);
        }
        LwIpLock();
        if (S->Pcb != NULL) {
            tcp_arg(S->Pcb, NULL);
            tcp_accept(S->Pcb, NULL);
            tcp_close(S->Pcb);
            S->Pcb = NULL;
        }
    } else if (S->Pcb != NULL) {
        tcp_arg(S->Pcb, NULL);
        tcp_recv(S->Pcb, NULL);
        tcp_err(S->Pcb, NULL);
        if (tcp_close(S->Pcb) != ERR_OK) {
            tcp_abort(S->Pcb);
        }
        S->Pcb = NULL;
    }
    S->Used = 0;
    S->Phase = 0;
    S->RxLen = 0;
    S->IsListen = 0;
    LwIpUnlock();
    return 0;
}

#else

#include "TcpSocket.h"
#include "SerialLog.h"

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

int TcpSocketClose(int Sock) {
    (void)Sock;
    return -1;
}

#endif
