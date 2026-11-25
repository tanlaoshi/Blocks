/*
 * HttpKeepAlive.c — 课堂对照入口：Connection: close 新连 vs keep-alive 同 socket
 *
 * 生产 Store HttpGet 仍走 close；shell `http keepalive` 调本入口。
 */
#include "StoreNetPrivate.h"
#include "LwIp.h"
#include "Console.h"
#include "Store.h"

int HttpKeepAliveBuildGet(const char *Path, int KeepAlive, char *Out, int OutMax) {
    const char *Prefix = "GET ";
    const char *Suffix;
    const char *Cursor;
    int Length = 0;

    Suffix = KeepAlive ? " HTTP/1.1\r\nHost: blocks\r\nConnection: keep-alive\r\n\r\n"
                       : " HTTP/1.0\r\nHost: blocks\r\nConnection: close\r\n\r\n";
    while (*Prefix && Length < OutMax - 1) {
        Out[Length++] = *Prefix++;
    }
    Cursor = Path;
    if (*Cursor != '/') {
        Out[Length++] = '/';
    }
    while (*Cursor && Length < OutMax - 48) {
        Out[Length++] = *Cursor++;
    }
    while (*Suffix && Length < OutMax - 1) {
        Out[Length++] = *Suffix++;
    }
    Out[Length] = 0;
    return Length;
}

/*
 * 成功 0。串口打 close 两次新连 + keep-alive 同 socket 两次 GET 对照。
 * Path 例："/" 或 "/catalog.txt"；keep-alive 须对端回 Content-Length。
 */
int HttpKeepAliveContrast(UINT32 Ip, UINT16 Port, const char *Path) {
    UINT8 *Resp;
    UINT32 Pages;
    int Rc;

    if (!Path || Path[0] == 0) {
        return STORE_ERR_NET;
    }
    if (!HalNetReady()) {
        HalConsoleWriteSerial("http: net not ready\n");
        return STORE_ERR_NET;
    }
    Pages = (STORE_HTTP_MAX + 4095u) / 4096u;
    Resp = (UINT8 *)PhysicalMemoryAllocatePages(Pages);
    if (!Resp) {
        return STORE_ERR_ALLOC;
    }

    HalConsoleWriteSerial("http: demo close — new TCP each GET\n");
    if (LwIpActive()) {
        Rc = HttpKeepAliveLwIpCloseGet(Ip, Port, Path, Resp, STORE_HTTP_MAX, 1);
        if (Rc == 0) {
            Rc = HttpKeepAliveLwIpCloseGet(Ip, Port, Path, Resp, STORE_HTTP_MAX, 2);
        }
        if (Rc == 0) {
            Rc = HttpKeepAliveLwIpTwice(Ip, Port, Path, Resp, STORE_HTTP_MAX);
        }
    } else {
        Rc = HttpKeepAliveNativeCloseGet(Ip, Port, Path, Resp, STORE_HTTP_MAX, 1);
        if (Rc == 0) {
            Rc = HttpKeepAliveNativeCloseGet(Ip, Port, Path, Resp, STORE_HTTP_MAX, 2);
        }
        if (Rc == 0) {
            Rc = HttpKeepAliveNativeTwice(Ip, Port, Path, Resp, STORE_HTTP_MAX);
        }
    }
    PhysicalMemoryFreePages(Resp, Pages);
    if (Rc == 0) {
        HalConsoleWriteSerial("http: demo done (close=new-conn, keep-alive=reuse)\n");
    } else {
        HalConsoleWriteSerial("http: demo fail\n");
    }
    return Rc;
}
