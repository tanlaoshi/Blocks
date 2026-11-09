/*
 * StoreNetHttp.c — HTTP/1.0 GET（内建 TCP / lwIP）
 * 成功 0；正文在堆页内，调用方 FreePages。
 * 收包 / 解析见 HttpGetReceive.c / HttpGetParse.c。
 * 核心：Net.c
 */
#include "StoreNetPrivate.h"
#include "LwIp.h"
#include "Console.h"

int HttpGet(UINT32 Ip, UINT16 Port, const char *Path, UINT8 **OutBody, UINTN *OutLen,
            UINT32 *OutPages) {
    UINT8 *Resp;
    UINT32 Pages;
    UINTN Got = 0;
    int Rc;

    if (!Path || !OutBody || !OutLen || !OutPages) {
        return STORE_ERR_NET;
    }
    *OutBody = 0;
    *OutLen = 0;
    *OutPages = 0;

    if (!HalNetReady()) {
        HalConsoleWriteSerial("store: net not ready\n");
        return STORE_ERR_NET;
    }

    Pages = (STORE_HTTP_MAX + 4095u) / 4096u;
    Resp = (UINT8 *)PhysicalMemoryAllocatePages(Pages);
    if (!Resp) {
        HalConsoleWriteSerial("store: alloc fail\n");
        return STORE_ERR_ALLOC;
    }

    /* lwip on：自研 Tcp 的 NetSendIp 恒失败；改走 LwIpSocket */
    if (LwIpActive()) {
        UINTN LwGot = 0;

        Rc = HttpGetLwIp(Ip, Port, Path, Resp, STORE_HTTP_MAX, &LwGot);
        if (Rc != 0) {
            PhysicalMemoryFreePages(Resp, Pages);
            return Rc;
        }
        Got = LwGot;
    } else {
        Rc = HttpGetReceiveNative(Ip, Port, Path, Resp, Pages, &Got);
        if (Rc != 0) {
            return Rc;
        }
    }

    return HttpGetParseResponse(Resp, Got, Pages, OutBody, OutLen, OutPages);
}
