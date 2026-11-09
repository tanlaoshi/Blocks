/*
 * HttpGetReceive.c — 自研 TCP 路径：连接 / 发 GET / 收满响应
 *
 * 自 Http.c::HttpGet 纯搬；lwIP 路径仍走 HttpGetLwIp。
 */
#include "StoreNetPrivate.h"
#include "Scheduler.h"
#include "LwIp.h"
#include "Console.h"

static char gHttpRequest[256]; /* 避免 HttpGet 再占任务栈 */

static void PollNet(void) {
    HalNetPoll();
    TcpPoll();
}

static void HttpBreath(void) {
    SchedulerIoBreath();
    if (LwIpActive()) {
        LwIpService();
    } else {
        PollNet();
    }
}

static int WaitEstablished(int Tries) {
    int N = 0;

    while (Tries-- > 0) {
        PollNet();
        if ((++N & 0x3FF) == 0) {
            HttpBreath();
        }
        if (TcpStateGet() == TCP_ESTABLISHED) {
            return 0;
        }
        if (TcpStateGet() != TCP_SYN_SENT) {
            return STORE_ERR_NET;
        }
    }
    return STORE_ERR_NET;
}

static int BuildAndSendGet(const char *Path) {
    int RequestLength = 0;
    const char *P;
    const char *A = "GET ";
    const char *B = " HTTP/1.0\r\nHost: blocks\r\nConnection: close\r\n\r\n";

    while (*A && RequestLength < (int)sizeof(gHttpRequest) - 1) {
        gHttpRequest[RequestLength++] = *A++;
    }
    P = Path;
    if (*P != '/') {
        gHttpRequest[RequestLength++] = '/';
    }
    while (*P && RequestLength < (int)sizeof(gHttpRequest) - 40) {
        gHttpRequest[RequestLength++] = *P++;
    }
    while (*B && RequestLength < (int)sizeof(gHttpRequest) - 1) {
        gHttpRequest[RequestLength++] = *B++;
    }
    gHttpRequest[RequestLength] = 0;
    if (TcpSend(gHttpRequest, (UINTN)RequestLength) != 0) {
        return STORE_ERR_NET;
    }
    return 0;
}

static int ReceiveUntilComplete(UINT8 *Resp, UINTN *OutGot) {
    UINTN Got = 0;
    UINTN Chunk;
    int Tries = STORE_HTTP_TRIES;
    int Rc;
    UINTN BodyOff = 0;
    UINTN BodyLen = 0;
    int HaveLen = 0;
    int Idle = 0;
    int Spin = 0;
    int Warm = 2000;

    /* 发送后立刻排空 RX，避免首段在进入循环前堆积/丢失 */
    while (Warm-- > 0) {
        PollNet();
    }

    while (Tries-- > 0 && Got < STORE_HTTP_MAX) {
        PollNet();
        if ((++Spin & 0x3FF) == 0) {
            HttpBreath();
        }
        Chunk = 0;
        (void)TcpRecv(Resp + Got, STORE_HTTP_MAX - Got, &Chunk);
        Got += Chunk;
        if (Chunk > 0) {
            Idle = 0;
        } else {
            Idle++;
            if (Idle > 0 && (Idle % STORE_HTTP_IDLE_ACK) == 0) {
                (void)TcpAckSend(); /* 催促对端重传缺失段 */
                HttpBreath();
            }
        }
        if (Got >= 16) {
            Rc = FindBody(Resp, Got, &BodyOff, &BodyLen, &HaveLen);
            if (Rc == -2) {
                if (OutGot != 0) {
                    *OutGot = Got;
                }
                return STORE_ERR_HTTP;
            }
            if (Rc == 0 && HaveLen && BodyOff + BodyLen <= Got) {
                break;
            }
            if (Rc == 0 && !HaveLen && TcpPeerClosed() && Idle > STORE_HTTP_IDLE_ACK) {
                break;
            }
        }
        if (HaveLen && BodyOff + BodyLen <= Got) {
            break;
        }
        /* 无 Content-Length：对端已关且空闲一会儿再结束 */
        if (!HaveLen && TcpPeerClosed() && Chunk == 0 && Got > 0 &&
            Idle > STORE_HTTP_IDLE_ACK * 2) {
            break;
        }
    }
    if (OutGot != 0) {
        *OutGot = Got;
    }
    return 0;
}

/*
 * 成功：*OutGot 为完整响应字节；调用方仍持有 Resp。
 * 失败：已 TcpClose + FreePages(Resp)；返回 STORE_ERR_*。
 */
int HttpGetReceiveNative(UINT32 Ip, UINT16 Port, const char *Path, UINT8 *Resp,
                         UINT32 Pages, UINTN *OutGot) {
    int Rc;

    if (OutGot != 0) {
        *OutGot = 0;
    }
    if (TcpStateGet() != TCP_CLOSED) {
        TcpClose();
    }
    if (TcpConnect(Ip, Port) != 0) {
        PhysicalMemoryFreePages(Resp, Pages);
        HalConsoleWriteSerial("store: tcp syn fail\n");
        return STORE_ERR_NET;
    }
    if (WaitEstablished(STORE_HTTP_TRIES) != 0) {
        TcpClose();
        PhysicalMemoryFreePages(Resp, Pages);
        HalConsoleWriteSerial("store: tcp connect timeout\n");
        return STORE_ERR_NET;
    }
    if (BuildAndSendGet(Path) != 0) {
        TcpClose();
        PhysicalMemoryFreePages(Resp, Pages);
        HalConsoleWriteSerial("store: tcp send fail\n");
        return STORE_ERR_NET;
    }
    Rc = ReceiveUntilComplete(Resp, OutGot);
    if (Rc == STORE_ERR_HTTP) {
        PhysicalMemoryFreePages(Resp, Pages);
        TcpClose();
        return STORE_ERR_HTTP;
    }
    TcpClose();
    return 0;
}
