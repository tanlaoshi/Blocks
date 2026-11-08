/*
 * TcpSocketIo.c — TcpSocket Send / Recv / Close
 */
#include "lwip/opt.h"

#if LWIP_TCP

#include "TcpSocketPrivate.h"

int TcpSocketSend(int Sock, const void *Data, UINTN Len) {
    TOY_SOCK *S = SockGet(Sock);
    err_t Err;
    UINTN Sent = 0;
    /*
     * Wi‑Fi poll 路径：等 sndbuf/ACK 时勿 Halt（见下）。
     * 单次调用墙钟宜短，好让上层（ksend Transport）轮询 Ctrl-C / 换策略；
     * 整次传输出 Transport 的 DRAIN/Retry 封顶（曾 60s 卡死壳 2026-11-03）。
     */
    UINT32 WaitStart = HalMilliseconds();
    const UINT32 WaitLimitMilliseconds = 2000u;

    if (S == NULL || Data == NULL) {
        return -EINVAL;
    }
    while (Sent < Len) {
        UINTN Chunk;
        u16_t Avail;
        int Phase;

        /* 有进度就重算墙钟，避免慢 Wi‑Fi 上「总耗时>2s」被误截成短写 */
        if ((UINT32)(HalMilliseconds() - WaitStart) >= WaitLimitMilliseconds) {
            break;
        }
        LwIpLock();
        if (S->Phase != 1 || S->Pcb == NULL) {
            LwIpUnlock();
            return Sent > 0 ? (int)Sent : -ENOTCONN;
        }
        Avail = tcp_sndbuf(S->Pcb);
        if (Avail == 0) {
            LwIpUnlock();
            /*
             * 勿 HalCpuHalt：iwl/I219 真机多为 poll，Halt 只等 IRQ 会饿死
             * NetPoll → ACK 不进、sndbuf 永不腾（NUC Wi‑Fi 表发死等 2026-11-03）。
             */
            LwIpService();
            SchedulerIoBreath();
            HalCpuRelax();
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
            SchedulerIoBreath();
            HalCpuRelax();
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
        WaitStart = HalMilliseconds();
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
    UINTN N;
    UINTN i;
    int Forever;
    UINT32 WaitStart;
    UINT32 WaitLimitMilliseconds;

    if (Sock < 0 || Sock >= TCP_SOCK_MAX || Buf == NULL || Len == 0) {
        return -EINVAL;
    }
    /*
     * TimeoutMs < 0：非阻塞；==0：一直等；>0：墙钟毫秒。
     * 勿 HalCpuHalt（poll 网卡）；阻塞路径须 IoBreath 泵鼠。
     */
    if (TimeoutMs < 0) {
        Forever = 0;
        WaitLimitMilliseconds = 0u;
    } else if (TimeoutMs == 0) {
        Forever = 1;
        WaitLimitMilliseconds = 0u;
    } else {
        Forever = 0;
        WaitLimitMilliseconds = (UINT32)TimeoutMs;
    }
    WaitStart = HalMilliseconds();
    for (;;) {
        TOY_SOCK *S;

        LwIpLock();
        /* 每圈重取：IoBreath 后勿沿用跨调用的 S 指针/槽号寄存器 */
        S = SockGet(Sock);
        if (S == NULL) {
            LwIpUnlock();
            return -EINVAL;
        }
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
        if (!Forever) {
            if (TimeoutMs < 0) {
                return 0;
            }
            if ((UINT32)(HalMilliseconds() - WaitStart) >= WaitLimitMilliseconds) {
                return 0;
            }
        }
        LwIpService();
        SchedulerIoBreath();
        HalCpuRelax();
    }
}

int TcpSocketFlushOut(int Sock, int TimeoutMs) {
    TOY_SOCK *S;
    UINT32 WaitStart;
    UINT32 WaitLimitMilliseconds;
    u16_t QueueLength;

    /*
     * 等 snd_queuelen==0：unsent/unacked 段都走完（对端 ACK）。
     * 仅 TcpSocketSend 写入 sndbuf 就返回时，Wi‑Fi 上 READY 行常到不了 PC
     * （串口镜像已见 READY，TCP 仍 timeout waiting for a line，2026-11-03）。
     */
    if (TimeoutMs < 0) {
        TimeoutMs = 0;
    }
    WaitStart = HalMilliseconds();
    WaitLimitMilliseconds = (TimeoutMs == 0) ? 30000u : (UINT32)TimeoutMs;
    for (;;) {
        LwIpLock();
        S = SockGet(Sock);
        if (S == NULL || S->Pcb == NULL || S->Phase != 1) {
            LwIpUnlock();
            return -1;
        }
        QueueLength = S->Pcb->snd_queuelen;
        (void)tcp_output(S->Pcb);
        LwIpUnlock();
        if (QueueLength == 0u) {
            return 0;
        }
        if ((UINT32)(HalMilliseconds() - WaitStart) >= WaitLimitMilliseconds) {
            return -1;
        }
        LwIpService();
        SchedulerIoBreath();
        HalCpuRelax();
    }
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

int TcpSocketAbort(int Sock) {
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
            (void)TcpSocketAbort(Pending[i]);
        }
        LwIpLock();
        SockAbortPcb(S);
    } else {
        SockAbortPcb(S);
    }
    S->Used = 0;
    S->Phase = 0;
    S->RxLen = 0;
    S->IsListen = 0;
    LwIpUnlock();
    return 0;
}

#endif /* LWIP_TCP */
