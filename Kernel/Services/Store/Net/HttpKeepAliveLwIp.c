/*
 * HttpKeepAliveLwIp.c — lwIP socket 上的 close 新连 / keep-alive 同 socket
 */
#include "StoreNetPrivate.h"
#include "Scheduler.h"
#include "LwIp.h"
#include "Console.h"

static char gLwIpRequest[256];

static int ReceiveUntilLength(int Sock, UINT8 *Resp, UINTN Cap, UINTN *OutGot,
                              int KeepAlive) {
    UINTN Got = 0;
    int Tries = 20000;
    int Idle = 0;
    int N;
    UINTN BodyOff = 0;
    UINTN BodyLen = 0;
    int HaveLen = 0;
    int Rc;

    while (Tries-- > 0 && Got < Cap) {
        N = LwIpSocketRecv(Sock, Resp + Got, Cap - Got, -1);
        if (N > 0) {
            Got += (UINTN)N;
            Idle = 0;
            if (Got >= 16) {
                Rc = FindBody(Resp, Got, &BodyOff, &BodyLen, &HaveLen);
                if (Rc == -2) {
                    if (OutGot) {
                        *OutGot = Got;
                    }
                    return STORE_ERR_HTTP;
                }
                if (KeepAlive && Rc == 0 && !HaveLen) {
                    HalConsoleWriteSerial("http: keep-alive needs Content-Length\n");
                    if (OutGot) {
                        *OutGot = Got;
                    }
                    return STORE_ERR_HTTP;
                }
                if (Rc == 0 && HaveLen && BodyOff + BodyLen <= Got) {
                    break;
                }
            }
            SchedulerIoBreath();
            continue;
        }
        if (N == -2 || N < 0) {
            break;
        }
        Idle++;
        LwIpService();
        SchedulerIoBreath();
        if (HaveLen && BodyOff + BodyLen <= Got) {
            break;
        }
        if (!KeepAlive && !HaveLen && Got > 0 && Idle > 2000) {
            break;
        }
        if (Got == 0 && Idle > 2500) {
            break;
        }
    }
    if (OutGot) {
        *OutGot = Got;
    }
    if (KeepAlive) {
        return (HaveLen && BodyOff + BodyLen <= Got) ? 0 : STORE_ERR_NET;
    }
    return Got == 0 ? STORE_ERR_NET : 0;
}

int HttpKeepAliveLwIpCloseGet(UINT32 Ip, UINT16 Port, const char *Path, UINT8 *Resp,
                              UINTN Cap, int Which) {
    int Sock;
    int RequestLength;
    UINTN Got = 0;
    int Rc;

    HalConsoleWriteSerial(Which == 1 ? "http: close get#1 tcp=new\n"
                                     : "http: close get#2 tcp=new\n");
    Sock = LwIpSocketCreate();
    if (Sock < 0) {
        return STORE_ERR_NET;
    }
    if (LwIpSocketConnect(Sock, Ip, Port) != 0) {
        LwIpSocketClose(Sock);
        return STORE_ERR_NET;
    }
    RequestLength =
        HttpKeepAliveBuildGet(Path, 0, gLwIpRequest, (int)sizeof(gLwIpRequest));
    if (RequestLength <= 0 ||
        LwIpSocketSend(Sock, gLwIpRequest, (UINTN)RequestLength) < 0) {
        LwIpSocketClose(Sock);
        return STORE_ERR_NET;
    }
    Rc = ReceiveUntilLength(Sock, Resp, Cap, &Got, 0);
    LwIpSocketClose(Sock);
    return Rc;
}

int HttpKeepAliveLwIpTwice(UINT32 Ip, UINT16 Port, const char *Path, UINT8 *Resp,
                           UINTN Cap) {
    int Sock;
    int RequestLength;
    int Round;
    UINTN Got;
    int Rc;

    HalConsoleWriteSerial("http: demo keep-alive — reuse TCP\n");
    Sock = LwIpSocketCreate();
    if (Sock < 0) {
        return STORE_ERR_NET;
    }
    if (LwIpSocketConnect(Sock, Ip, Port) != 0) {
        LwIpSocketClose(Sock);
        return STORE_ERR_NET;
    }
    for (Round = 1; Round <= 2; Round++) {
        HalConsoleWriteSerial(Round == 1 ? "http: keepalive get#1 same-socket\n"
                                         : "http: keepalive get#2 same-socket (no SYN)\n");
        RequestLength =
            HttpKeepAliveBuildGet(Path, 1, gLwIpRequest, (int)sizeof(gLwIpRequest));
        if (RequestLength <= 0 ||
            LwIpSocketSend(Sock, gLwIpRequest, (UINTN)RequestLength) < 0) {
            LwIpSocketClose(Sock);
            return STORE_ERR_NET;
        }
        Got = 0;
        Rc = ReceiveUntilLength(Sock, Resp, Cap, &Got, 1);
        if (Rc != 0) {
            LwIpSocketClose(Sock);
            return Rc;
        }
    }
    LwIpSocketClose(Sock);
    return 0;
}
