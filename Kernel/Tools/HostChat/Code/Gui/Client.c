/*
 * Client.c — TCP 连中继并发送 HOSTCHAT-GUI 握手
 */
#include "Client.h"

#include "Relay.h"
#include "Serve.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int HostChatClientConnect(const char *Host, unsigned short Port) {
    int SocketDescriptor;
    struct sockaddr_in Address;
    char Hello[64];
    char Reply[64];
    ssize_t Got;
    size_t Off = 0;

    SocketDescriptor = socket(AF_INET, SOCK_STREAM, 0);
    if (SocketDescriptor < 0) {
        return -1;
    }
    memset(&Address, 0, sizeof(Address));
    Address.sin_family = AF_INET;
    Address.sin_port = htons(Port);
    if (inet_pton(AF_INET, Host != NULL ? Host : "127.0.0.1", &Address.sin_addr) !=
        1) {
        close(SocketDescriptor);
        return -1;
    }
    if (connect(SocketDescriptor, (struct sockaddr *)&Address, sizeof(Address)) < 0) {
        close(SocketDescriptor);
        return -1;
    }
    snprintf(Hello, sizeof(Hello), "%s\n", HOST_CHAT_GUI_HELLO);
    if (HostChatClientSendLine(SocketDescriptor, Hello) != 0) {
        close(SocketDescriptor);
        return -1;
    }
    /* 等 OK GUI\n */
    while (Off + 1 < sizeof(Reply)) {
        Got = HostChatSocketRead(SocketDescriptor, Reply + Off, 1);
        if (Got <= 0) {
            close(SocketDescriptor);
            return -1;
        }
        if (Reply[Off] == '\n') {
            Reply[Off] = '\0';
            break;
        }
        Off += (size_t)Got;
    }
    if (strncmp(Reply, "OK GUI", 6) != 0) {
        close(SocketDescriptor);
        return -1;
    }
    HostChatSetNonBlocking(SocketDescriptor);
    return SocketDescriptor;
}

int HostChatClientSendLine(int SocketDescriptor, const char *Line) {
    size_t Length;
    size_t Off = 0;

    if (SocketDescriptor < 0 || Line == NULL) {
        return -1;
    }
    Length = strlen(Line);
    while (Off < Length) {
        ssize_t Written = HostChatSocketWrite(SocketDescriptor, Line + Off, Length - Off);
        if (Written <= 0) {
            return -1;
        }
        Off += (size_t)Written;
    }
    return 0;
}

int HostChatClientReadAvailable(int SocketDescriptor, char *Buffer,
                                size_t Capacity) {
    ssize_t Got;

    if (SocketDescriptor < 0 || Buffer == NULL || Capacity == 0) {
        return -1;
    }
    Got = HostChatSocketRead(SocketDescriptor, Buffer, Capacity);
    if (Got < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        return -1;
    }
    return (int)Got;
}
