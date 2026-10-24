/*
 * chatd — 宿主机半双工对端（listen :9090；stdin ↔ 一行 TCP）
 * 编：cc -O2 -o chatd chatd.c
 * 跑：./chatd          # 默认 9090
 *      ./chatd 9090
 */
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

static int ListenPort(unsigned short Port) {
    int Fd, One = 1;
    struct sockaddr_in Addr;

    Fd = socket(AF_INET, SOCK_STREAM, 0);
    if (Fd < 0) {
        perror("socket");
        return -1;
    }
    setsockopt(Fd, SOL_SOCKET, SO_REUSEADDR, &One, sizeof(One));
    memset(&Addr, 0, sizeof(Addr));
    Addr.sin_family = AF_INET;
    Addr.sin_addr.s_addr = htonl(INADDR_ANY);
    Addr.sin_port = htons(Port);
    if (bind(Fd, (struct sockaddr *)&Addr, sizeof(Addr)) < 0) {
        perror("bind");
        close(Fd);
        return -1;
    }
    if (listen(Fd, 1) < 0) {
        perror("listen");
        close(Fd);
        return -1;
    }
    return Fd;
}

static int Pump(int Conn) {
    char Buf[512];
    fd_set Rd;
    int Max;
    ssize_t N;

    for (;;) {
        FD_ZERO(&Rd);
        FD_SET(STDIN_FILENO, &Rd);
        FD_SET(Conn, &Rd);
        Max = Conn > STDIN_FILENO ? Conn : STDIN_FILENO;
        if (select(Max + 1, &Rd, 0, 0, 0) < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("select");
            return -1;
        }
        if (FD_ISSET(Conn, &Rd)) {
            N = read(Conn, Buf, sizeof(Buf));
            if (N <= 0) {
                fprintf(stderr, "chatd: peer closed\n");
                return 0;
            }
            if (fwrite(Buf, 1, (size_t)N, stdout) != (size_t)N) {
                return -1;
            }
            fflush(stdout);
        }
        if (FD_ISSET(STDIN_FILENO, &Rd)) {
            N = read(STDIN_FILENO, Buf, sizeof(Buf));
            if (N <= 0) {
                return 0;
            }
            if (write(Conn, Buf, (size_t)N) != N) {
                perror("write");
                return -1;
            }
        }
    }
}

int main(int Argc, char **Argv) {
    unsigned short Port = 9090;
    int Listen, Conn;
    struct sockaddr_in Peer;
    socklen_t PeerLen = sizeof(Peer);

    if (Argc >= 2) {
        Port = (unsigned short)atoi(Argv[1]);
        if (Port == 0) {
            fprintf(stderr, "usage: %s [port]\n", Argv[0]);
            return 1;
        }
    }
    Listen = ListenPort(Port);
    if (Listen < 0) {
        return 1;
    }
    fprintf(stderr, "chatd: listen *:%u (wait Guest)\n", Port);
    Conn = accept(Listen, (struct sockaddr *)&Peer, &PeerLen);
    close(Listen);
    if (Conn < 0) {
        perror("accept");
        return 1;
    }
    fprintf(stderr, "chatd: connected from %s\n", inet_ntoa(Peer.sin_addr));
    Pump(Conn);
    close(Conn);
    return 0;
}
