/*
 * Serve.c — HostChat 半双工 TCP（listen → accept → 读写）
 */
#include "Serve.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int HostChatCreateListenSocket(unsigned short Port) {
    int SocketDescriptor;
    int Reuse = 1;
    struct sockaddr_in Address;

    SocketDescriptor = socket(AF_INET, SOCK_STREAM, 0);
    if (SocketDescriptor < 0) {
        return -1;
    }
    if (setsockopt(SocketDescriptor, SOL_SOCKET, SO_REUSEADDR, &Reuse, sizeof(Reuse)) < 0) {
        close(SocketDescriptor);
        return -1;
    }
    memset(&Address, 0, sizeof(Address));
    Address.sin_family = AF_INET;
    Address.sin_addr.s_addr = htonl(INADDR_ANY);
    Address.sin_port = htons(Port);
    if (bind(SocketDescriptor, (struct sockaddr *)&Address, sizeof(Address)) < 0) {
        close(SocketDescriptor);
        return -1;
    }
    if (listen(SocketDescriptor, 16) < 0) {
        close(SocketDescriptor);
        return -1;
    }
    return SocketDescriptor;
}

int HostChatConnectPeer(const char *Host, unsigned short Port) {
    int SocketDescriptor;
    struct sockaddr_in Address;

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
    return SocketDescriptor;
}

int HostChatAcceptPeer(int ListenSocket, char *PeerAddress, size_t PeerAddressBytes) {
    struct sockaddr_in Peer;
    socklen_t PeerLength = sizeof(Peer);
    int Connection;

    Connection = accept(ListenSocket, (struct sockaddr *)&Peer, &PeerLength);
    if (Connection < 0) {
        return -1;
    }
    if (PeerAddress != NULL && PeerAddressBytes > 0) {
        const char *Text = inet_ntoa(Peer.sin_addr);
        size_t Index = 0;

        while (Text[Index] != '\0' && Index + 1 < PeerAddressBytes) {
            PeerAddress[Index] = Text[Index];
            Index++;
        }
        PeerAddress[Index] = '\0';
    }
    return Connection;
}

int HostChatSetNonBlocking(int SocketDescriptor) {
    int Flags;

    Flags = fcntl(SocketDescriptor, F_GETFL, 0);
    if (Flags < 0) {
        return -1;
    }
    if (fcntl(SocketDescriptor, F_SETFL, Flags | O_NONBLOCK) < 0) {
        return -1;
    }
    return 0;
}

ssize_t HostChatSocketRead(int SocketDescriptor, void *Buffer, size_t Length) {
    return read(SocketDescriptor, Buffer, Length);
}

ssize_t HostChatSocketWrite(int SocketDescriptor, const void *Buffer, size_t Length) {
    return write(SocketDescriptor, Buffer, Length);
}

void HostChatCloseSocket(int SocketDescriptor) {
    if (SocketDescriptor >= 0) {
        close(SocketDescriptor);
    }
}
