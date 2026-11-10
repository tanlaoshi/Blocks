/*
 * Serve.c — TCP 热更 listen 状态机（Settings 后台听 / 壳阻塞封装）
 *
 * Idle → Listening（Accept 切片）→ Transferring（同步 Transfer）→ Idle。
 * 板默认 reboot/backup 见 ServePreferences；主机 --no-* 覆盖见 ServeClient。
 */
#include "Ksend/Private.h"
#include "Ksend/TransportPrivate.h"
#include "LwIp.h"
#include "Hal.h"
#include "Scheduler.h"
#include "Tasks.h"

#define KSEND_ACCEPT_SLICE_MILLISECONDS 50u

static KSEND_SERVE_STATE sServeState = KSEND_SERVE_IDLE;
static int sListenSocket = -1;
static UINT16 sListenPort;
static UINT32 sWaitMilliseconds;
static UINT32 sListenStartMilliseconds;
static int sLastResult;
static int sPollBusy;

static void ServeCloseListenSocket(void) {
    if (sListenSocket >= 0) {
        (void)LwIpSocketClose(sListenSocket);
        sListenSocket = -1;
    }
}

static void ServeEnterIdle(int Result) {
    ServeCloseListenSocket();
    KsendTransportUseSerial();
    HalNetPinPrimary(0);
    sServeState = KSEND_SERVE_IDLE;
    sLastResult = Result;
    sWaitMilliseconds = 0;
}

KSEND_SERVE_STATE KsendServeState(void) {
    return sServeState;
}

int KsendServeLastResult(void) {
    return sLastResult;
}

UINT16 KsendServeListenPort(void) {
    return sListenPort;
}

void KsendServeRequestAbort(void) {
    gTransportUserAbort = 1;
    if (sServeState == KSEND_SERVE_LISTENING) {
        KsendNote("serve: stop (settings)");
        ServeEnterIdle(1);
    }
}

void KsendServeListenStop(void) {
    if (sServeState == KSEND_SERVE_IDLE) {
        return;
    }
    if (sServeState == KSEND_SERVE_LISTENING) {
        KsendNote("serve: listen stop");
        ServeEnterIdle(1);
        return;
    }
    gTransportUserAbort = 1;
}

int KsendServeListenStart(UINT16 Port, UINT32 WaitMilliseconds) {
    char NoteLine[64];
    KSEND_TEXT Out;

    if (sServeState != KSEND_SERVE_IDLE) {
        KsendServeListenStop();
        if (sServeState != KSEND_SERVE_IDLE) {
            return 1;
        }
    }
    if (Port == 0u) {
        Port = KsendServeDefaultPort();
    }
    if (!LwIpActive() && LwIpInitialize() != 0) {
        KsendNote("serve: lwip init fail");
        return 1;
    }
    HalNetPinPrimary(1);
    sListenSocket = LwIpSocketCreate();
    if (sListenSocket < 0) {
        KsendNote("serve: socket create fail");
        HalNetPinPrimary(0);
        return 1;
    }
    if (LwIpSocketBind(sListenSocket, 0u, Port) != 0) {
        KsendNote("serve: bind fail");
        ServeCloseListenSocket();
        HalNetPinPrimary(0);
        return 1;
    }
    if (LwIpSocketListen(sListenSocket, 1) != 0) {
        KsendNote("serve: listen fail");
        ServeCloseListenSocket();
        HalNetPinPrimary(0);
        return 1;
    }
    sListenPort = Port;
    sWaitMilliseconds = WaitMilliseconds;
    sListenStartMilliseconds = HalMilliseconds();
    sServeState = KSEND_SERVE_LISTENING;
    sLastResult = 0;
    gTransportUserAbort = 0;
    KsendTextReset(&Out, NoteLine, (int)sizeof(NoteLine));
    KsendTextPut(&Out, "serve: listen port=");
    KsendTextPutUnsigned(&Out, Port);
    KsendTextFlush(&Out);
    return 0;
}

void KsendServePoll(void) {
    int ClientSocket;
    UINT8 AbortByte;
    int Result;

    if (sPollBusy || sServeState == KSEND_SERVE_IDLE) {
        return;
    }
    sPollBusy = 1;
    if (sServeState != KSEND_SERVE_LISTENING) {
        sPollBusy = 0;
        return;
    }
    ClientSocket =
        LwIpSocketAccept(sListenSocket, (int)KSEND_ACCEPT_SLICE_MILLISECONDS);
    if (ClientSocket >= 0) {
        sServeState = KSEND_SERVE_TRANSFERRING;
        ServeCloseListenSocket();
        Result = KsendServeRunClient(ClientSocket);
        ServeEnterIdle(Result);
        sPollBusy = 0;
        return;
    }
    if (HalSerialTryGetByte(&AbortByte) != 0 && AbortByte == 0x03u) {
        KsendNote("serve: aborted (Ctrl-C)");
        ServeEnterIdle(1);
        sPollBusy = 0;
        return;
    }
    if (gTransportUserAbort) {
        KsendNote("serve: aborted");
        ServeEnterIdle(1);
        sPollBusy = 0;
        return;
    }
    if (sWaitMilliseconds != 0u &&
        ((UINT32)(HalMilliseconds() - sListenStartMilliseconds)) >=
            sWaitMilliseconds) {
        KsendNote("serve: accept timeout");
        ServeEnterIdle(1);
        sPollBusy = 0;
        return;
    }
    LwIpService();
    sPollBusy = 0;
}

int KsendServe(UINT16 Port, UINT32 WaitMilliseconds) {
    if (KsendServeListenStart(Port, WaitMilliseconds) != 0) {
        return 1;
    }
    while (KsendServeState() != KSEND_SERVE_IDLE) {
        KsendServePoll();
        if (KsendServeState() == KSEND_SERVE_LISTENING) {
            YieldForPollInput();
        }
    }
    return KsendServeLastResult();
}
