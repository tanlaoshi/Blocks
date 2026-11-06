/*
 * ChatDaemon.c — 宿主机 CLI peer（连中继；stdin ↔ TCP）；产物名 chatd
 */
#include "Serve.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

static int PumpConnected(int Connection) {
    char Buffer[512];
    fd_set ReadSet;
    int MaxDescriptor;
    ssize_t Count;

    for (;;) {
        FD_ZERO(&ReadSet);
        FD_SET(STDIN_FILENO, &ReadSet);
        FD_SET(Connection, &ReadSet);
        MaxDescriptor = Connection > STDIN_FILENO ? Connection : STDIN_FILENO;
        if (select(MaxDescriptor + 1, &ReadSet, NULL, NULL, NULL) < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("select");
            return -1;
        }
        if (FD_ISSET(Connection, &ReadSet)) {
            Count = HostChatSocketRead(Connection, Buffer, sizeof(Buffer));
            if (Count <= 0) {
                fprintf(stderr, "chatd: server closed\n");
                return 0;
            }
            if (fwrite(Buffer, 1, (size_t)Count, stdout) != (size_t)Count) {
                return -1;
            }
            fflush(stdout);
        }
        if (FD_ISSET(STDIN_FILENO, &ReadSet)) {
            Count = read(STDIN_FILENO, Buffer, sizeof(Buffer));
            if (Count <= 0) {
                return 0;
            }
            if (HostChatSocketWrite(Connection, Buffer, (size_t)Count) != Count) {
                perror("write");
                return -1;
            }
        }
    }
}

int main(int ArgumentCount, char **ArgumentVector) {
    const char *Host = "127.0.0.1";
    unsigned short Port = HOST_CHAT_DEFAULT_PORT;
    int Connection;

    if (ArgumentCount >= 2) {
        Host = ArgumentVector[1];
    }
    if (ArgumentCount >= 3) {
        Port = (unsigned short)atoi(ArgumentVector[2]);
        if (Port == 0) {
            fprintf(stderr, "usage: %s [host] [port]\n", ArgumentVector[0]);
            return 1;
        }
    }
    Connection = HostChatConnectPeer(Host, Port);
    if (Connection < 0) {
        perror("connect");
        fprintf(stderr, "chatd: connect %s:%u failed (start hostchat-gui server first)\n",
                Host, Port);
        return 1;
    }
    fprintf(stderr, "chatd: connected %s:%u (peer)\n", Host, Port);
    PumpConnected(Connection);
    HostChatCloseSocket(Connection);
    return 0;
}
