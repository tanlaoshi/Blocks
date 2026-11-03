/*
 * TcpSocketAcceptConnect.c — Accept / Connect
 */
#include "lwip/opt.h"

#if LWIP_TCP

#include "TcpSocketPrivate.h"

int TcpSocketAccept(int Sock, int TimeoutMs) {
    TOY_SOCK *S = SockGet(Sock);
    int Child;
    int i;
    int Forever;
    UINT32 WaitStart;
    UINT32 WaitLimitMilliseconds;

    if (S == NULL || S->Phase != 4) {
        return -1;
    }
    /*
     * 勿 HalCpuHalt：iwl 等 poll 网卡靠 LwIpService→NetPoll；Halt 只等 IRQ
     * 会饿死 SYN 收包（与 TcpSocketSend sndbuf 同病，2026-11-03）。
     * TimeoutMs 按墙钟毫秒，不再当 Halt 次数。
     */
    Forever = (TimeoutMs <= 0);
    WaitStart = HalMilliseconds();
    WaitLimitMilliseconds = Forever ? 0u : (UINT32)TimeoutMs;
    while (S->PendingCount == 0 && S->Phase == 4) {
        LwIpService();
        SchedulerIoBreath();
        HalCpuRelax();
        if (!Forever &&
            (UINT32)(HalMilliseconds() - WaitStart) >= WaitLimitMilliseconds) {
            break;
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
     * 忙泵 + 墙钟等待：ARP/SYN-ACK 常已在 RX 环。
     * 勿 HalCpuHalt（poll 网卡同 Send/Accept）；TimeoutMs 按毫秒墙钟。
     * CoopDrain 下须 IoBreath 泵鼠，否则 exec SOCKDEMO 会饿死 xHCI。
     */
    {
        int Burst = 800;
        UINT32 WaitStart;
        UINT32 WaitLimitMilliseconds;

        while (S->Phase == 0 && Burst-- > 0) {
            LwIpService();
            if ((Burst & 63) == 0) {
                SchedulerIoBreath();
            }
        }
        WaitStart = HalMilliseconds();
        WaitLimitMilliseconds = TimeoutMs > 0 ? (UINT32)TimeoutMs : 8000u;
        while (S->Phase == 0 &&
               (UINT32)(HalMilliseconds() - WaitStart) < WaitLimitMilliseconds) {
            LwIpService();
            SchedulerIoBreath();
            HalCpuRelax();
        }
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


#endif /* LWIP_TCP */
