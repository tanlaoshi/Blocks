/*
 * ShellCommandsNetUdp.c — PR-S3-shellnet-1：udp listen / send（从 Net.c 搬家）
 */
#include "ShellPrivate.h"
#include "Console.h"
#include "Hal.h"
#include "Udp.h"
#include "LwIp.h"
#include "HalDevices.h"

static void CommandUdpListen(int Argc, char **Argv) {
    UINT32 Port = 0;
    if (Argc < 2) {
        gConsole.Write("usage: udplisten <port>\n");
        return;
    }
    for (const char *P = Argv[1]; *P; P++) {
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
#ifdef LWIP_ENABLE
    if (LwIpActive()) {
        if (LwIpUdpBind((UINT16)Port) != 0) {
            gConsole.Write("udplisten: failed\n");
            return;
        }
        gConsole.Write("lwip: udp listening ");
        gConsole.WriteHex32(Port);
        gConsole.Write("\n");
        return;
    }
#endif
    UdpBind((UINT16)Port);
    gConsole.Write("udp: listening ");
    gConsole.WriteHex32(Port);
    gConsole.Write("\n");
}

static void CommandUdpSend(int Argc, char **Argv) {
    UINT32 Ip;
    UINT32 Port = 0;
    if (Argc < 4) {
        gConsole.Write("usage: udpsend <ip> <port> <text>\n");
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
    {
        UINTN Len = 0;
        while (Argv[3][Len]) {
            Len++;
        }
#ifdef LWIP_ENABLE
        if (LwIpActive()) {
            if (LwIpUdpSend(Ip, (UINT16)Port, Argv[3], Len) != 0) {
                gConsole.Write("udpsend failed\n");
            } else {
                gConsole.Write("udp: sent\n");
            }
            return;
        }
#endif
        if (UdpSend(Ip, (UINT16)Port, Argv[3], Len) != 0) {
            gConsole.Write("udpsend failed\n");
        } else {
            gConsole.Write("udp: sent\n");
        }
    }
}

void ShellCommandsNetUdpRegister(void) {
    gConsole.Register2("udp", "listen", "bind UDP port", CommandUdpListen);
    gConsole.Register2("udp", "send", "send UDP datagram", CommandUdpSend);
    gConsole.RegisterAliasLine("udplisten", "udp", "listen");
    gConsole.RegisterAliasLine("udpsend", "udp", "send");
}
