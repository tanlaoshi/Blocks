/*
 * Serve.c — 网络热更：TCP listen → 一连接 → 同串口 ksend 协议
 *
 * 绑定 INADDR_ANY：有线 / Wi‑Fi 只要主网栈有址即可。
 * PC：`ksend.py --host <ip>`；板：`ksend serve`（可先 `ip` 看址）。
 */
#include "Ksend/Private.h"
#include "LwIp.h"
#include "Hal.h"
#include "Scheduler.h"

#define KSEND_TCP_PORT_DEFAULT 26992u
#define KSEND_SERVE_LINE_BYTES 128u
#define KSEND_ACCEPT_SLICE_MILLISECONDS 250u

static int ArgumentIs(const char *Argument, const char *Word) {
    if (Argument == 0 || Word == 0) {
        return 0;
    }
    while (*Argument != 0 && *Word != 0) {
        char Left = *Argument;
        char Right = *Word;

        if (Left >= 'A' && Left <= 'Z') {
            Left = (char)(Left - 'A' + 'a');
        }
        if (Right >= 'A' && Right <= 'Z') {
            Right = (char)(Right - 'A' + 'a');
        }
        if (Left != Right) {
            return 0;
        }
        Argument++;
        Word++;
    }
    return *Argument == 0 && *Word == 0;
}

static int ReadCommandLine(char *Line, int Capacity) {
    int Length = 0;
    UINT8 Byte;

    if (Line == 0 || Capacity < 2) {
        return -1;
    }
    for (;;) {
        if (KsendTransportGetByte(&Byte, 30000u) == 0) {
            return -1;
        }
        if (Byte == (UINT8)'\n') {
            break;
        }
        if (Byte == (UINT8)'\r') {
            continue;
        }
        if (Length + 1 < Capacity) {
            Line[Length++] = (char)Byte;
        }
    }
    Line[Length] = 0;
    return Length;
}

/* 0=transfer；1=info；2=selftest；-1=坏行 */
static int ParseServeCommand(char *Line, const char **TargetPath, int *Reboot,
                             int *Backup) {
    char *Cursor;
    char *Token;
    int SawKsend = 0;
    int Mode = 0;

    *TargetPath = 0;
    *Reboot = 1;
    *Backup = 1;
    Cursor = Line;
    while (*Cursor != 0) {
        while (*Cursor == ' ' || *Cursor == '\t') {
            Cursor++;
        }
        if (*Cursor == 0) {
            break;
        }
        Token = Cursor;
        while (*Cursor != 0 && *Cursor != ' ' && *Cursor != '\t') {
            Cursor++;
        }
        if (*Cursor != 0) {
            *Cursor = 0;
            Cursor++;
        }
        if (!SawKsend) {
            /* PC Tools/Scripts/ksend.py 发 ksend；板端壳名 HotSend 亦可 */
            if (!ArgumentIs(Token, "ksend") && !ArgumentIs(Token, "hotsend")) {
                return -1;
            }
            SawKsend = 1;
            continue;
        }
        if (ArgumentIs(Token, "--no-reboot")) {
            *Reboot = 0;
        } else if (ArgumentIs(Token, "--no-backup")) {
            *Backup = 0;
        } else if (ArgumentIs(Token, "info")) {
            Mode = 1;
        } else if (ArgumentIs(Token, "selftest")) {
            Mode = 2;
        } else if (ArgumentIs(Token, "serve")) {
            return -1;
        } else {
            *TargetPath = Token;
        }
    }
    if (!SawKsend) {
        return -1;
    }
    return Mode;
}

int KsendServe(UINT16 Port, UINT32 WaitMilliseconds) {
    int ListenSocket;
    int ClientSocket;
    UINT32 Start;
    char Line[KSEND_SERVE_LINE_BYTES];
    const char *TargetPath;
    int Reboot;
    int Backup;
    int Result;
    char NoteLine[64];
    KSEND_TEXT Out;

    if (Port == 0u) {
        Port = (UINT16)KSEND_TCP_PORT_DEFAULT;
    }
    if (!LwIpActive() && LwIpInitialize() != 0) {
        KsendNote("serve: lwip init fail");
        return 1;
    }
    /*
     * 钉住当前主槽：serve 阻塞期间若有线链路抖一下，SelectPrimary 会把 TX
     * 切回有线，Wi‑Fi 上的 TCP 回程变黑洞（NUC 真机）。
     */
    HalNetPinPrimary(1);
    ListenSocket = LwIpSocketCreate();
    if (ListenSocket < 0) {
        KsendNote("serve: socket create fail");
        HalNetPinPrimary(0);
        return 1;
    }
    if (LwIpSocketBind(ListenSocket, 0u, Port) != 0) {
        KsendNote("serve: bind fail");
        (void)LwIpSocketClose(ListenSocket);
        HalNetPinPrimary(0);
        return 1;
    }
    if (LwIpSocketListen(ListenSocket, 1) != 0) {
        KsendNote("serve: listen fail");
        (void)LwIpSocketClose(ListenSocket);
        HalNetPinPrimary(0);
        return 1;
    }

    KsendTextReset(&Out, NoteLine, (int)sizeof(NoteLine));
    KsendTextPut(&Out, "serve: listen port=");
    KsendTextPutUnsigned(&Out, Port);
    KsendTextFlush(&Out);

    Start = HalMilliseconds();
    ClientSocket = -1;
    for (;;) {
        UINT8 AbortByte;

        ClientSocket = LwIpSocketAccept(ListenSocket, (int)KSEND_ACCEPT_SLICE_MILLISECONDS);
        if (ClientSocket >= 0) {
            break;
        }
        if (HalSerialTryGetByte(&AbortByte) != 0 && AbortByte == 0x03u) {
            KsendNote("serve: aborted (Ctrl-C)");
            (void)LwIpSocketClose(ListenSocket);
            HalNetPinPrimary(0);
            return 1;
        }
        if (WaitMilliseconds != 0u &&
            ((UINT32)(HalMilliseconds() - Start)) >= WaitMilliseconds) {
            KsendNote("serve: accept timeout");
            (void)LwIpSocketClose(ListenSocket);
            HalNetPinPrimary(0);
            return 1;
        }
        LwIpService();
        SchedulerIoBreath();
    }
    (void)LwIpSocketClose(ListenSocket);

    KsendTransportUseSocket(ClientSocket);
    Result = ReadCommandLine(Line, (int)sizeof(Line));
    if (Result < 0) {
        KsendNote("serve: command timeout");
        KsendTransportUseSerial();
        HalNetPinPrimary(0);
        return 1;
    }
    Result = ParseServeCommand(Line, &TargetPath, &Reboot, &Backup);
    if (Result < 0) {
        KsendNote("serve: bad command line");
        KsendTransportUseSerial();
        HalNetPinPrimary(0);
        return 1;
    }
    if (Result == 1) {
        Result = KsendInfo();
    } else if (Result == 2) {
        Result = KsendSelfTest();
    } else {
        Result = KsendTransfer(TargetPath, Reboot, Backup);
    }
    KsendTransportUseSerial();
    HalNetPinPrimary(0);
    return Result;
}

UINT16 KsendServeDefaultPort(void) {
    return (UINT16)KSEND_TCP_PORT_DEFAULT;
}
