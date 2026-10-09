/*
 * LwIpSock.c — K43：lwIP TCP 客户端（对标现网 toy_socket 客户端子集）
 *
 * 【初学者】socket→connect→send/recv→close；无 bind/listen（后刀）。
 */
#include "lwip/opt.h"
#include "LwIpSock.h"
#include "LwIp.h"
#include "LwIpAddr.h"

#if LWIP_TCP

#include "lwip/tcp.h"
#include "lwip/pbuf.h"
#include "lwip/ip_addr.h"

#define SOCK_RX_MAX 4096

/* Phase: 0 connecting, 1 connected, 2 peer-closed, -1 error */
typedef struct {
    int Used;
    struct tcp_pcb *Pcb;
    volatile int Phase;
    volatile err_t Err;
    UINT8 Rx[SOCK_RX_MAX];
    UINTN RxLen;
} LWIP_SOCK;

static LWIP_SOCK gSocks[LWIP_SOCK_MAX];

static LWIP_SOCK *SockGet(int Sock) {
    if (Sock < 0 || Sock >= LWIP_SOCK_MAX || !gSocks[Sock].Used) {
        return 0;
    }
    return &gSocks[Sock];
}

static int SockAllocSlot(void) {
    int i;

    for (i = 0; i < LWIP_SOCK_MAX; i++) {
        if (!gSocks[i].Used) {
            gSocks[i].Used = 1;
            gSocks[i].Pcb = NULL;
            gSocks[i].Phase = 0;
            gSocks[i].Err = ERR_OK;
            gSocks[i].RxLen = 0;
            return i;
        }
    }
    return -1;
}

static void SockAbortPcb(LWIP_SOCK *S) {
    if (S->Pcb != NULL) {
        tcp_arg(S->Pcb, NULL);
        tcp_recv(S->Pcb, NULL);
        tcp_err(S->Pcb, NULL);
        tcp_abort(S->Pcb);
        S->Pcb = NULL;
    }
}

static void SockSetupConnected(LWIP_SOCK *S, struct tcp_pcb *Pcb);

static err_t SockRecvCb(void *Arg, struct tcp_pcb *Pcb, struct pbuf *P, err_t Err) {
    LWIP_SOCK *S = (LWIP_SOCK *)Arg;
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
    pbuf_free(P);
    return ERR_OK;
}

static void SockErrCb(void *Arg, err_t Err) {
    LWIP_SOCK *S = (LWIP_SOCK *)Arg;

    if (S == NULL) {
        return;
    }
    S->Pcb = NULL;
    S->Err = Err;
    S->Phase = -1;
}

static err_t SockConnectedCb(void *Arg, struct tcp_pcb *Pcb, err_t Err) {
    LWIP_SOCK *S = (LWIP_SOCK *)Arg;

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

static void SockSetupConnected(LWIP_SOCK *S, struct tcp_pcb *Pcb) {
    S->Pcb = Pcb;
    S->Phase = 1;
    S->RxLen = 0;
    tcp_arg(Pcb, S);
    tcp_recv(Pcb, SockRecvCb);
    tcp_err(Pcb, SockErrCb);
}

int LwIpSockCreate(void) {
    if (!LwIpActive() && LwIpInitialize() != 0) {
        return -1;
    }
    return SockAllocSlot();
}

int LwIpSockConnect(int Sock, UINT32 DstIp, UINT16 DstPort, int TimeoutMs) {
    LWIP_SOCK *S = SockGet(Sock);
    struct tcp_pcb *Pcb;
    ip4_addr_t A4;
    ip_addr_t Remote;
    err_t Err;
    int Tries;
    int Burst;

    if (S == NULL || DstPort == 0) {
        return -1;
    }
    if (S->Pcb == NULL) {
        Pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
        if (Pcb == NULL) {
            return -1;
        }
        S->Pcb = Pcb;
    } else {
        Pcb = S->Pcb;
    }
    S->Phase = 0;
    S->Err = ERR_OK;
    tcp_arg(Pcb, S);
    tcp_err(Pcb, SockErrCb);
    HostIpToLwIp(DstIp, &A4);
    ip_addr_copy_from_ip4(Remote, A4);
    Err = tcp_connect(Pcb, &Remote, DstPort, SockConnectedCb);
    if (Err != ERR_OK) {
        SockAbortPcb(S);
        S->Phase = -1;
        return -1;
    }
    Burst = 800;
    while (S->Phase == 0 && Burst-- > 0) {
        LwIpService();
    }
    Tries = TimeoutMs > 0 ? TimeoutMs : 8000;
    while (S->Phase == 0 && Tries-- > 0) {
        LwIpService();
        __asm__ volatile("pause");
    }
    if (S->Phase != 1) {
        SockAbortPcb(S);
        S->Phase = -1;
        return -1;
    }
    return 0;
}

int LwIpSockSend(int Sock, const void *Data, UINTN Len) {
    LWIP_SOCK *S = SockGet(Sock);
    err_t Err;
    UINTN Sent = 0;
    int Tries = 2000;

    if (S == NULL || Data == NULL) {
        return -1;
    }
    while (Sent < Len && Tries-- > 0) {
        UINTN Chunk;
        u16_t Avail;

        if (S->Phase != 1 || S->Pcb == NULL) {
            return Sent > 0 ? (int)Sent : -1;
        }
        Avail = tcp_sndbuf(S->Pcb);
        if (Avail == 0) {
            LwIpService();
            __asm__ volatile("pause");
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
            LwIpService();
            continue;
        }
        if (Err != ERR_OK) {
            return Sent > 0 ? (int)Sent : -1;
        }
        (void)tcp_output(S->Pcb);
        Sent += Chunk;
        LwIpService();
    }
    return Sent > 0 ? (int)Sent : -1;
}

int LwIpSockRecv(int Sock, void *Buf, UINTN Len, int TimeoutMs) {
    LWIP_SOCK *S = SockGet(Sock);
    UINTN N;
    UINTN i;
    int Tries;

    if (S == NULL || Buf == NULL || Len == 0) {
        return -1;
    }
    Tries = TimeoutMs > 0 ? TimeoutMs : 3000;
    while (Tries-- > 0) {
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
            return (int)N;
        }
        if (S->Phase == 2) {
            return -2;
        }
        if (S->Phase < 0) {
            return -1;
        }
        if (S->Phase != 1) {
            return 0;
        }
        LwIpService();
        __asm__ volatile("pause");
    }
    return 0;
}

int LwIpSockClose(int Sock) {
    LWIP_SOCK *S = SockGet(Sock);

    if (S == NULL) {
        return -1;
    }
    if (S->Pcb != NULL) {
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
    return 0;
}

#else

int LwIpSockCreate(void) {
    return -1;
}
int LwIpSockConnect(int Sock, UINT32 DstIp, UINT16 DstPort, int TimeoutMs) {
    (void)Sock;
    (void)DstIp;
    (void)DstPort;
    (void)TimeoutMs;
    return -1;
}
int LwIpSockSend(int Sock, const void *Data, UINTN Len) {
    (void)Sock;
    (void)Data;
    (void)Len;
    return -1;
}
int LwIpSockRecv(int Sock, void *Buf, UINTN Len, int TimeoutMs) {
    (void)Sock;
    (void)Buf;
    (void)Len;
    (void)TimeoutMs;
    return -1;
}
int LwIpSockClose(int Sock) {
    (void)Sock;
    return -1;
}

#endif
