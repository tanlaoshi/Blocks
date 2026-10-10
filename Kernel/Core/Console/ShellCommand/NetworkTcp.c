/*
 * NetworkTcp.c — Shell：tcplisten / tcpconnect
 *
 * 【初学者】
 * - 薄包装 Core/Network/Tcp.c（builtin 单连接；lwIP on 后 TcpPoll 空转）
 * - 对外入口：NetworkTcpRegister
 */
#include "ShellCommand.h"
#include "Network.h"
#include "HalSerial.h"

static void Put(const char *S) {
    HalSerialWriteShell(S);
}

static void PutUnsigned32(UINT32 V) {
    char Digits[12];
    int Count = 0;
    int Index;

    if (V == 0) {
        Put("0");
        return;
    }
    while (V != 0 && Count < 11) {
        Digits[Count++] = (char)('0' + (V % 10u));
        V /= 10u;
    }
    for (Index = Count - 1; Index >= 0; Index--) {
        char One[2];
        One[0] = Digits[Index];
        One[1] = 0;
        Put(One);
    }
}

static int ParsePort(const char *S, UINT32 *Out) {
    UINT32 V = 0;
    if (S == 0 || *S == 0) {
        return -1;
    }
    while (*S) {
        if (*S < '0' || *S > '9') {
            return -1;
        }
        V = V * 10u + (UINT32)(*S - '0');
        if (V > 65535u) {
            return -1;
        }
        S++;
    }
    if (V == 0) {
        return -1;
    }
    *Out = V;
    return 0;
}

static void CommandTcpListen(int Argc, char **Argv) {
    UINT32 Port = 0;

    if (Argc < 2 || ParsePort(Argv[1], &Port) != 0) {
        Put("usage: tcplisten <port>\n");
        return;
    }
    if (!NetworkNicReady()) {
        Put("tcp: no nic\n");
        return;
    }
    if (TcpListen((UINT16)Port) != 0) {
        Put("tcp: listen fail\n");
        return;
    }
    Put("tcp: listening ");
    PutUnsigned32(Port);
    Put(" (echo; host nc 127.0.0.1:15000 if port=5000)\n");
}

/* tcpconnect <ip> <port> <text> */
static void CommandTcpConnect(int Argc, char **Argv) {
    UINT32 Ip;
    UINT32 Port = 0;
    UINTN Len = 0;
    int Tries;

    if (Argc < 4) {
        Put("usage: tcpconnect <ip> <port> <text>\n");
        return;
    }
    if (!NetworkNicReady()) {
        Put("tcp: no nic\n");
        return;
    }
    if (NetworkParseIp(Argv[1], &Ip) != 0) {
        Put("tcp: bad ip\n");
        return;
    }
    if (ParsePort(Argv[2], &Port) != 0) {
        Put("tcp: bad port\n");
        return;
    }
    while (Argv[3][Len]) {
        Len++;
    }
    if (TcpGetState() == NETWORK_TCP_LISTEN) {
        Put("tcp: closing listen (single slot)\n");
    }
    if (TcpConnect(Ip, (UINT16)Port) != 0) {
        Put("tcpconnect: syn failed\n");
        return;
    }
    for (Tries = 0; Tries < 4000; Tries++) {
        TcpPoll(2);
        if (TcpGetState() == NETWORK_TCP_ESTABLISHED) {
            break;
        }
        if (TcpGetState() == NETWORK_TCP_CLOSED) {
            Put("tcpconnect: closed\n");
            return;
        }
    }
    if (TcpGetState() != NETWORK_TCP_ESTABLISHED) {
        Put("tcpconnect: timeout\n");
        TcpClose();
        return;
    }
    if (TcpSend(Argv[3], Len) != 0) {
        Put("tcpconnect: send failed\n");
        TcpClose();
        return;
    }
    for (Tries = 0; Tries < 1000; Tries++) {
        TcpPoll(2);
    }
    TcpClose();
    Put("tcpconnect: done\n");
}

/*
 * NetworkTcpRegister — 注册 tcplisten / tcpconnect
 *
 * 谁调用：ShellCommandInitialize。
 * 返回：void
 */
void NetworkTcpRegister(void) {
    ShellCommandRegister("tcplisten", "TCP echo listen", CommandTcpListen);
    ShellCommandRegister("tcpconnect", "TCP connect+send", CommandTcpConnect);
}
