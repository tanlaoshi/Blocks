/*
 * NetHttp.c — 课堂 HTTP keep-alive 对照（shell `http keepalive`）
 */
#include "ShellPrivate.h"
#include "Console.h"
#include "Hal.h"
#include "Store.h"

static void CommandHttpKeepAlive(int Argc, char **Argv) {
    UINT32 Ip = 0;
    UINT16 Port = 0;
    const char *Path = "/";
    int Rc;

    if (!HalNetReady()) {
        gConsole.Write("http: net not ready\n");
        return;
    }
    StoreRepoGet(&Ip, &Port);
    if (Argc >= 2) {
        if (HalNetParseIp(Argv[1], &Ip) != 0) {
            /* 单参数当作 path；仓库 IP/Port */
            Path = Argv[1];
        } else if (Argc >= 3) {
            UINT32 ParsedPort = 0;
            const char *Cursor;

            for (Cursor = Argv[2]; *Cursor; Cursor++) {
                if (*Cursor < '0' || *Cursor > '9') {
                    gConsole.Write("usage: http keepalive [path]|[ip port [path]]\n");
                    return;
                }
                ParsedPort = ParsedPort * 10u + (UINT32)(*Cursor - '0');
            }
            if (ParsedPort == 0 || ParsedPort > 65535u) {
                gConsole.Write("bad port\n");
                return;
            }
            Port = (UINT16)ParsedPort;
            if (Argc >= 4) {
                Path = Argv[3];
            }
        } else {
            gConsole.Write("usage: http keepalive [path]|[ip port [path]]\n");
            return;
        }
    }
    gConsole.Write("http keepalive demo…\n");
    Rc = HttpKeepAliveContrast(Ip, Port, Path);
    gConsole.Write(Rc == 0 ? "http keepalive: ok (see serial)\n"
                           : "http keepalive: fail (see serial)\n");
}

void ShellCommandsNetHttpRegister(void) {
    gConsole.Register2("http", "keepalive",
                       "close vs keep-alive contrast (serial)", CommandHttpKeepAlive);
}
