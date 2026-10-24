/* ChatNet.c — peer=BLOCKS.DB + 行发送 */
#include "Net.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <Blocks/File.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <Net.h>

static int ReadDec(const char **Pp, unsigned *Out) {
    const char *P = *Pp;
    unsigned V = 0;
    int Dig = 0;

    while (*P >= '0' && *P <= '9') {
        V = V * 10u + (unsigned)(*P - '0');
        P++;
        Dig = 1;
    }
    if (V > 65535u) {
        return -1;
    }
    if (!Dig) {
        return -1;
    }
    *Out = V;
    *Pp = P;
    return 0;
}

static int ParsePeer(const char *Val, unsigned *OutIp, unsigned short *OutPort) {
    const char *P = Val;
    unsigned A, B, C, D, Port = CHAT_PORT_DEF;

    if (!Val || !OutIp || !OutPort) {
        return -1;
    }
    if (ReadDec(&P, &A) != 0 || *P++ != '.') {
        return -1;
    }
    if (ReadDec(&P, &B) != 0 || *P++ != '.') {
        return -1;
    }
    if (ReadDec(&P, &C) != 0 || *P++ != '.') {
        return -1;
    }
    if (ReadDec(&P, &D) != 0) {
        return -1;
    }
    if (*P == ':') {
        P++;
        if (ReadDec(&P, &Port) != 0) {
            return -1;
        }
    }
    while (*P == ' ' || *P == '\t') {
        P++;
    }
    if (*P != 0 || A > 255 || B > 255 || C > 255 || D > 255
        || Port == 0 || Port > 65535) {
        return -1;
    }
    *OutIp = NetIpv4(A, B, C, D);
    *OutPort = (unsigned short)Port;
    return 0;
}

/*
 * 整文件一次 FileRead 再按行扫。禁止逐字节 Read+写 Out[N]：
 * O2 把 Out 放 r14 跨 syscall，曾见返回后 r14 变成内核地址 → #PF。
 */
int ChatLoadPeer(unsigned *OutIp, unsigned short *OutPort) {
    unsigned Ip;
    unsigned short Port;
    int Fd;
    int Found = 0;
    char Buf[512];
    ssize_t N;
    ssize_t I;
    ssize_t LineStart;

    if (!OutIp || !OutPort) {
        return -1;
    }
    Ip = NetIpv4(10, 0, 2, 2);
    Port = CHAT_PORT_DEF;

    Fd = FileOpen("BLOCKS.DB");
    if (Fd < 0) {
        Fd = FileOpen("/BLOCKS.DB");
    }
    if (Fd >= 0) {
        N = FileRead(Fd, Buf, sizeof(Buf) - 1);
        FileClose(Fd);
        if (N < 0) {
            N = 0;
        }
        Buf[N] = 0;
        LineStart = 0;
        for (I = 0; I <= N; I++) {
            char C = (I < N) ? Buf[I] : '\n';
            if (C != '\n' && C != '\r' && I < N) {
                continue;
            }
            if (I > LineStart) {
                char Line[128];
                ssize_t Len = I - LineStart;
                char *P;
                if (Len >= (ssize_t)sizeof(Line)) {
                    Len = (ssize_t)sizeof(Line) - 1;
                }
                memcpy(Line, Buf + LineStart, (size_t)Len);
                Line[Len] = 0;
                P = Line;
                while (*P == ' ' || *P == '\t') {
                    P++;
                }
                if (strncmp(P, "chat.peer=", 10) == 0) {
                    P += 10;
                    while (*P == ' ' || *P == '\t') {
                        P++;
                    }
                    if (ParsePeer(P, &Ip, &Port) != 0) {
                        printf("chat: bad chat.peer=%s\n", P);
                        return -1;
                    }
                    Found = 1;
                    break;
                }
            }
            LineStart = I + 1;
        }
    }

    *OutIp = Ip;
    *OutPort = Port;
    if (!Found) {
        printf("chat: no chat.peer; default 10.0.2.2:%u\n", (unsigned)Port);
    }
    return 0;
}

int ChatSendLine(int Fd, const char *Line, size_t Len) {
    char Buf[CHAT_LINE_MAX + 2];
    size_t Off = 0;
    size_t I;

    if (Len > CHAT_LINE_MAX) {
        return 0;
    }
    for (I = 0; I < Len; I++) {
        Buf[I] = Line[I];
    }
    Buf[Len] = '\n';
    while (Off < Len + 1) {
        ssize_t W = send(Fd, Buf + Off, Len + 1 - Off, 0);
        if (W <= 0) {
            return -1;
        }
        Off += (size_t)W;
    }
    return 0;
}
