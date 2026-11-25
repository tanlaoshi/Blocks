/*
 * HttpKeepAliveNative.c — 自研 Tcp 上的 close 新连 / keep-alive 同 socket
 */
#include "StoreNetPrivate.h"
#include "Scheduler.h"
#include "LwIp.h"
#include "Console.h"

static char gNativeRequest[256];

static void PollNative(void) {
    HalNetPoll();
    TcpPoll();
}

static void Breath(void) {
    SchedulerIoBreath();
    if (LwIpActive()) {
        LwIpService();
    } else {
        PollNative();
    }
}

static int WaitEstablished(int Tries) {
    int Spin = 0;

    while (Tries-- > 0) {
        PollNative();
        if ((++Spin & 0x3FF) == 0) {
            Breath();
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

static int ReceiveOne(UINT8 *Resp, UINTN Cap, UINTN *OutGot, int KeepAlive) {
    UINTN Got = 0;
    UINTN Chunk;
    int Tries = STORE_HTTP_TRIES;
    int Idle = 0;
    int Spin = 0;
    int Warm = 2000;
    UINTN BodyOff = 0;
    UINTN BodyLen = 0;
    int HaveLen = 0;
    int Rc;

    while (Warm-- > 0) {
        PollNative();
    }
    while (Tries-- > 0 && Got < Cap) {
        PollNative();
        if ((++Spin & 0x3FF) == 0) {
            Breath();
        }
        Chunk = 0;
        (void)TcpRecv(Resp + Got, Cap - Got, &Chunk);
        Got += Chunk;
        if (Chunk > 0) {
            Idle = 0;
        } else {
            Idle++;
            if (Idle > 0 && (Idle % STORE_HTTP_IDLE_ACK) == 0) {
                (void)TcpAckSend();
                Breath();
            }
        }
        if (Got < 16) {
            continue;
        }
        Rc = FindBody(Resp, Got, &BodyOff, &BodyLen, &HaveLen);
        if (Rc == -2) {
            if (OutGot) {
                *OutGot = Got;
            }
            return STORE_ERR_HTTP;
        }
        if (Rc == 0 && HaveLen && BodyOff + BodyLen <= Got) {
            break;
        }
        if (KeepAlive) {
            if (Rc == 0 && !HaveLen) {
                HalConsoleWriteSerial("http: keep-alive needs Content-Length\n");
                if (OutGot) {
                    *OutGot = Got;
                }
                return STORE_ERR_HTTP;
            }
            continue;
        }
        if (Rc == 0 && !HaveLen && TcpPeerClosed() && Idle > STORE_HTTP_IDLE_ACK) {
            break;
        }
        if (!HaveLen && TcpPeerClosed() && Chunk == 0 && Got > 0 &&
            Idle > STORE_HTTP_IDLE_ACK * 2) {
            break;
        }
    }
    if (OutGot) {
        *OutGot = Got;
    }
    return 0;
}

int HttpKeepAliveNativeCloseGet(UINT32 Ip, UINT16 Port, const char *Path, UINT8 *Resp,
                                UINTN Cap, int Which) {
    int RequestLength;
    int Rc;
    UINTN Got = 0;

    HalConsoleWriteSerial(Which == 1 ? "http: close get#1 tcp=new\n"
                                     : "http: close get#2 tcp=new\n");
    if (TcpStateGet() != TCP_CLOSED) {
        TcpClose();
    }
    if (TcpConnect(Ip, Port) != 0) {
        return STORE_ERR_NET;
    }
    if (WaitEstablished(STORE_HTTP_TRIES) != 0) {
        TcpClose();
        return STORE_ERR_NET;
    }
    RequestLength =
        HttpKeepAliveBuildGet(Path, 0, gNativeRequest, (int)sizeof(gNativeRequest));
    if (RequestLength <= 0 || TcpSend(gNativeRequest, (UINTN)RequestLength) != 0) {
        TcpClose();
        return STORE_ERR_NET;
    }
    Rc = ReceiveOne(Resp, Cap, &Got, 0);
    TcpClose();
    return Rc;
}

int HttpKeepAliveNativeTwice(UINT32 Ip, UINT16 Port, const char *Path, UINT8 *Resp,
                             UINTN Cap) {
    int RequestLength;
    int Rc;
    UINTN Got = 0;

    HalConsoleWriteSerial("http: demo keep-alive — reuse TCP\n");
    if (TcpStateGet() != TCP_CLOSED) {
        TcpClose();
    }
    if (TcpConnect(Ip, Port) != 0) {
        return STORE_ERR_NET;
    }
    if (WaitEstablished(STORE_HTTP_TRIES) != 0) {
        TcpClose();
        return STORE_ERR_NET;
    }

    HalConsoleWriteSerial("http: keepalive get#1 same-socket\n");
    RequestLength =
        HttpKeepAliveBuildGet(Path, 1, gNativeRequest, (int)sizeof(gNativeRequest));
    if (RequestLength <= 0 || TcpSend(gNativeRequest, (UINTN)RequestLength) != 0) {
        TcpClose();
        return STORE_ERR_NET;
    }
    Rc = ReceiveOne(Resp, Cap, &Got, 1);
    if (Rc != 0) {
        TcpClose();
        return Rc;
    }

    HalConsoleWriteSerial("http: keepalive get#2 same-socket (no SYN)\n");
    RequestLength =
        HttpKeepAliveBuildGet(Path, 1, gNativeRequest, (int)sizeof(gNativeRequest));
    if (RequestLength <= 0 || TcpSend(gNativeRequest, (UINTN)RequestLength) != 0) {
        TcpClose();
        return STORE_ERR_NET;
    }
    Got = 0;
    Rc = ReceiveOne(Resp, Cap, &Got, 1);
    TcpClose();
    return Rc;
}
