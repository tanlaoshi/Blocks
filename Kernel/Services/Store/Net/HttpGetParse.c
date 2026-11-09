/*
 * HttpGetParse.c — HTTP 响应头解析 / 正文收口（自 Http.c::HttpGet 纯搬）
 */
#include "StoreNetPrivate.h"
#include "Console.h"

static int RecoverBodyWithoutHeader(UINT8 *Resp, UINTN Got, UINTN *BodyOff,
                                    UINTN *BodyLen, int *HaveLen) {
    UINTN i;
    int Rc = -1;

    /* 缓冲里找 HTTP/（头被偏移错切时） */
    for (i = 0; i + 5 < Got; i++) {
        if (Resp[i] == 'H' && Resp[i + 1] == 'T' && Resp[i + 2] == 'T' &&
            Resp[i + 3] == 'P' && Resp[i + 4] == '/') {
            Rc = FindBody(Resp + i, Got - i, BodyOff, BodyLen, HaveLen);
            if (Rc == 0) {
                *BodyOff += i;
                return 0;
            }
        }
    }
    /* 仅收到正文：catalog 以 # 开头，ELF 魔数 */
    if (Resp[0] == '#' ||
        (Got >= 4 && Resp[0] == 0x7F && Resp[1] == 'E' && Resp[2] == 'L' &&
         Resp[3] == 'F')) {
        *BodyOff = 0;
        *BodyLen = Got;
        *HaveLen = 1;
        HalConsoleWriteSerial("store: raw body fallback\n");
        return 0;
    }
    return Rc;
}

static void NotifyTruncated(UINTN Got, UINTN Need) {
    char Msg[80];
    int n = 0;
    const char *P = "store: truncated got=";

    while (*P) {
        Msg[n++] = *P++;
    }
    /* 十进制长度便于对照 Content-Length */
    {
        UINT32 V = (UINT32)Got;
        char Tmp[12];
        int t = 0;
        if (V == 0) {
            Tmp[t++] = '0';
        } else {
            while (V) {
                Tmp[t++] = (char)('0' + (V % 10));
                V /= 10;
            }
        }
        while (t > 0) {
            Msg[n++] = Tmp[--t];
        }
    }
    Msg[n++] = '/';
    {
        UINT32 V = (UINT32)Need;
        char Tmp[12];
        int t = 0;
        if (V == 0) {
            Tmp[t++] = '0';
        } else {
            while (V) {
                Tmp[t++] = (char)('0' + (V % 10));
                V /= 10;
            }
        }
        while (t > 0) {
            Msg[n++] = Tmp[--t];
        }
    }
    Msg[n++] = '\n';
    Msg[n] = 0;
    /* Worker 异步：gConsole.Notify 先换行离开 blocks>，勿用裸串口插在提示符后 */
    gConsole.Notify(Msg);
}

static int RejectBadHttp(UINT8 *Resp, UINTN Got, UINT32 Pages, int Rc) {
    HalConsoleWriteSerial("store: bad http got=");
    HalConsoleWriteSerial(Got > 0 ? "nz\n" : "0\n");
    if (Got >= 4) {
        char Snap[8];
        Snap[0] = (char)Resp[0];
        Snap[1] = (char)Resp[1];
        Snap[2] = (char)Resp[2];
        Snap[3] = (char)Resp[3];
        Snap[4] = 0;
        HalConsoleWriteSerial("store: head ");
        HalConsoleWriteSerial(Snap);
        HalConsoleWriteSerial("\n");
    }
    PhysicalMemoryFreePages(Resp, Pages);
    return Rc == -2 ? STORE_ERR_HTTP : STORE_ERR_NET;
}

/*
 * 成功：正文前移到 Resp[0..]；填 Out*。
 * 失败：已 FreePages；返回 STORE_ERR_*。
 */
int HttpGetParseResponse(UINT8 *Resp, UINTN Got, UINT32 Pages, UINT8 **OutBody,
                         UINTN *OutLen, UINT32 *OutPages) {
    int Rc;
    UINTN BodyOff = 0;
    UINTN BodyLen = 0;
    int HaveLen = 0;

    Rc = FindBody(Resp, Got, &BodyOff, &BodyLen, &HaveLen);
    if (Rc != 0 && Got > 0) {
        if (RecoverBodyWithoutHeader(Resp, Got, &BodyOff, &BodyLen, &HaveLen) ==
            0) {
            Rc = 0;
        }
    }
    if (Rc != 0) {
        return RejectBadHttp(Resp, Got, Pages, Rc);
    }
    if (HaveLen) {
        if (BodyOff + BodyLen > Got) {
            NotifyTruncated(Got, BodyOff + BodyLen);
            PhysicalMemoryFreePages(Resp, Pages);
            return STORE_ERR_NET;
        }
    } else {
        BodyLen = Got > BodyOff ? Got - BodyOff : 0;
    }
    if (BodyLen == 0) {
        PhysicalMemoryFreePages(Resp, Pages);
        HalConsoleWriteSerial("store: empty body\n");
        return STORE_ERR_NET;
    }

    {
        UINTN i;
        for (i = 0; i < BodyLen; i++) {
            Resp[i] = Resp[BodyOff + i];
        }
    }
    *OutBody = Resp;
    *OutLen = BodyLen;
    *OutPages = Pages;
    return 0;
}
