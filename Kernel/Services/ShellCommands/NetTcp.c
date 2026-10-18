/*
 * ShellCommandsNetTcp.c — PR-S3-shellnet-1：tcp + ShellOnInterrupt（从 Net.c 搬家）
 */
#include "ShellPrivate.h"
#include "Console.h"
#include "Hal.h"
#include "Tcp.h"
#include "LwIp.h"
#include "HalDevices.h"

static int ArgIsStop(const char *S) {
    return S[0] == 's' && S[1] == 't' && S[2] == 'o' && S[3] == 'p' && S[4] == 0;
}

void ShellOnInterrupt(void) {
#ifdef TOY_LWIP
    if (LwIpActive()) {
        if (LwIpTcpListenStop() == 0) {
            gConsole.Write("lwip: echo server stopped\n");
            gConsole.DiscardInput();
            gConsole.ForceResumePrompt();
            return;
        }
    }
#endif
    if (TcpStateGet() == TCP_LISTEN) {
        TcpListenStop();
        gConsole.Write("tcp: echo server stopped\n");
        gConsole.DiscardInput();
        gConsole.ForceResumePrompt();
        return;
    }
    /* 挂起中但已非 LISTEN（曾叠层 Suspend）：仍恢复提示符 */
    if (gConsole.PromptSuspended()) {
        gConsole.Write("tcp: echo server stopped\n");
        gConsole.DiscardInput();
        gConsole.ForceResumePrompt();
        return;
    }
    gConsole.CancelInput();
}

static void CommandTcpListen(int Argc, char **Argv) {
    UINT32 Port = 0;
    if (Argc < 2) {
        gConsole.Write("usage: tcp listen <port>|stop\n");
        return;
    }
    if (ArgIsStop(Argv[1])) {
#ifdef TOY_LWIP
        if (LwIpActive()) {
            if (LwIpTcpListenStop() != 0) {
                gConsole.Write("tcplisten: not listening\n");
                return;
            }
        gConsole.Write("lwip: echo server stopped\n");
            gConsole.DiscardInput();
            gConsole.ForceResumePrompt();
            return;
        }
#endif
    if (TcpStateGet() != TCP_LISTEN) {
            gConsole.Write("tcplisten: not listening\n");
            return;
        }
        TcpListenStop();
        gConsole.Write("tcp: echo server stopped\n");
        gConsole.DiscardInput();
        gConsole.ForceResumePrompt();
        return;
    }
    for (const char *P = Argv[1]; *P; P++) {
        if (*P < '0' || *P > '9') {
            gConsole.Write("bad port\n");
            return;
        }
        Port = Port * 10 + (UINT32)(*P - '0');
    }
#ifdef TOY_LWIP
    if (LwIpActive()) {
        if (LwIpTcpListen((UINT16)Port) != 0) {
            gConsole.Write("tcplisten: failed\n");
            return;
        }
        if (!gConsole.PromptSuspended()) {
            gConsole.SuspendPrompt();
        }
        gConsole.Write("lwip: echo server on ");
        gConsole.WriteHex32(Port);
        gConsole.Write("\n");
        return;
    }
#endif
    TcpListen((UINT16)Port);
    if (!gConsole.PromptSuspended()) {
        gConsole.SuspendPrompt();
    }
    gConsole.Write("tcp: echo server on ");
    gConsole.WriteHex32(Port);
    gConsole.Write("\n");
}

static void CommandTcpStatus(int Argc, char **Argv) {
    char IpBuf[20];
    UINT32 Una;
    UINT32 Nxt;
    UINT32 BufLen;
    UINT16 PeerWnd;
    UINT8 Retrans;
    (void)Argc;
    (void)Argv;
#ifdef TOY_LWIP
    if (LwIpActive()) {
        gConsole.Write("tcpstatus: lwIP active (builtin idle)\n");
        gConsole.Write("  tcp listen=");
        gConsole.WriteHex32(LwIpTcpListenPort());
        gConsole.Write(" udp bind=");
        gConsole.WriteHex32(LwIpUdpBoundPort());
        gConsole.Write("\n");
        return;
    }
#endif
    TcpWindowStatsGet(&Una, &Nxt, &BufLen, &PeerWnd, &Retrans);
    gConsole.Write("tcp state=");
    gConsole.WriteHex32((UINT32)TcpStateGet());
    gConsole.Write(" local=");
    gConsole.WriteHex32(TcpLocalPort());
    gConsole.Write(" peer=");
    HalNetFormatIp(TcpPeerIp(), IpBuf, sizeof(IpBuf));
    gConsole.Write(IpBuf);
    gConsole.Write(":");
    gConsole.WriteHex32(TcpPeerPort());
    gConsole.Write("\n  snd_una=");
    gConsole.WriteHex32(Una);
    gConsole.Write(" snd_nxt=");
    gConsole.WriteHex32(Nxt);
    gConsole.Write(" buf=");
    gConsole.WriteHex32(BufLen);
    gConsole.Write(" peer_wnd=");
    gConsole.WriteHex32(PeerWnd);
    gConsole.Write(" retrans=");
    gConsole.WriteHex32(Retrans);
    gConsole.Write("\n");
}

static void CommandTcpConnect(int Argc, char **Argv) {
    UINT32 Ip;
    UINT32 Port = 0;
    UINTN TextLen;
    if (Argc < 4) {
        gConsole.Write("usage: tcp connect <ip> <port> <text>\n");
        return;
    }
#ifdef TOY_LWIP
    if (LwIpActive()) {
        UINTN TextLen;
        int Ret;

        if (!HalNetReady()) {
            gConsole.Write("Net: not available\n");
            return;
        }
        if (HalNetParseIp(Argv[1], &Ip) != 0) {
            gConsole.Write("bad ip\n");
            return;
        }
        for (const char *P = Argv[2]; *P; P++) {
            if (*P < '0' || *P > '9') {
                gConsole.Write("bad port\n");
                return;
            }
            Port = Port * 10 + (UINT32)(*P - '0');
        }
        if (Port == 0 || Port > 65535) {
            gConsole.Write("bad port\n");
            return;
        }
        TextLen = 0;
        while (Argv[3][TextLen]) {
            TextLen++;
        }
        Ret = LwIpTcpConnectSend(Ip, (UINT16)Port, Argv[3], TextLen, 3000);
        if (Ret == 0) {
            gConsole.Write("tcpconnect: done\n");
        } else if (Ret == -2) {
            gConsole.Write("tcpconnect: timeout\n");
        } else if (Ret == -3) {
            gConsole.Write("tcpconnect: send failed\n");
        } else {
            gConsole.Write("tcpconnect: syn failed\n");
            gConsole.Write("hint: on host run nc -l ");
            gConsole.WriteHex32(Port);
            gConsole.Write(" first\n");
        }
        return;
    }
#endif
    if (TcpStateGet() == TCP_LISTEN) {
        gConsole.Write("tcpconnect: closes tcplisten (single TCP slot)\n");
    }
    if (!HalNetReady()) {
        gConsole.Write("Net: not available\n");
        return;
    }
    if (HalNetParseIp(Argv[1], &Ip) != 0) {
        gConsole.Write("bad ip\n");
        return;
    }
    for (const char *P = Argv[2]; *P; P++) {
        if (*P < '0' || *P > '9') {
            gConsole.Write("bad port\n");
            return;
        }
        Port = Port * 10 + (UINT32)(*P - '0');
    }
    if (Port == 0 || Port > 65535) {
        gConsole.Write("bad port\n");
        return;
    }
    if (TcpConnect(Ip, (UINT16)Port) != 0) {
        gConsole.Write("tcpconnect: syn failed\n");
        return;
    }
    {
        int Tries = 3000;
        while (Tries-- > 0 && TcpStateGet() == TCP_SYN_SENT) {
            HalNetPoll();
            TcpPoll();
        }
    }
    if (TcpStateGet() != TCP_ESTABLISHED) {
        gConsole.Write("tcpconnect: timeout\n");
        return;
    }
    TextLen = 0;
    while (Argv[3][TextLen]) {
        TextLen++;
    }
    if (TcpSend(Argv[3], TextLen) != 0) {
        gConsole.Write("tcpconnect: send failed\n");
        return;
    }
    {
        int Tries = 3000;
        while (Tries-- > 0 && TcpStateGet() == TCP_ESTABLISHED) {
            HalNetPoll();
            TcpPoll();
        }
    }
    gConsole.Write("tcpconnect: done\n");
}

static void Note(const char *Text) {
    if (gConsole.Write != 0) {
        gConsole.Write(Text);
    }
}

void ShellCommandsNetTcpRegister(void) {
    TcpNoteRegister(Note);
    gConsole.Register2("tcp", "listen", "TCP echo server", CommandTcpListen);
    gConsole.Register2("tcp", "connect", "TCP connect and send", CommandTcpConnect);
    gConsole.Register2("tcp", "status", "TCP connection status", CommandTcpStatus);
    gConsole.RegisterAliasLine("tcplisten", "tcp", "listen");
    gConsole.RegisterAliasLine("tcpconnect", "tcp", "connect");
    gConsole.RegisterAliasLine("tcpstatus", "tcp", "status");
}
