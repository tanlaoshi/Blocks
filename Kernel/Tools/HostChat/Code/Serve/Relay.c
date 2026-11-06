/*
 * Relay.c — TCP 中继主循环：本机 GUI + Guest peer 都连此处
 */
#include "RelayInternal.h"

#include "Serve.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void HostChatRelayLog(HostChatRelayLogFunction Log, void *UserData,
                             const char *Text) {
    if (Log != NULL) {
        Log(Text, UserData);
    }
}

HostChatRelay *HostChatRelayCreate(unsigned short Port) {
    HostChatRelay *Relay;
    int Index;

    Relay = calloc(1, sizeof(*Relay));
    if (Relay == NULL) {
        return NULL;
    }
    Relay->Port = Port == 0 ? HOST_CHAT_DEFAULT_PORT : Port;
    Relay->ListenSocket = -1;
    Relay->NextPeerIdentifier = 1;
    for (Index = 0; Index < HOST_CHAT_MAX_CLIENTS; Index++) {
        Relay->Clients[Index].SocketDescriptor = -1;
    }
    return Relay;
}

void HostChatRelayDestroy(HostChatRelay *Relay) {
    int Index;

    if (Relay == NULL) {
        return;
    }
    HostChatRelayRequestStop(Relay);
    HostChatCloseSocket(Relay->ListenSocket);
    for (Index = 0; Index < HOST_CHAT_MAX_CLIENTS; Index++) {
        HostChatCloseSocket(Relay->Clients[Index].SocketDescriptor);
    }
    free(Relay);
}

void HostChatRelayRequestStop(HostChatRelay *Relay) {
    if (Relay != NULL) {
        Relay->Stop = 1;
        HostChatCloseSocket(Relay->ListenSocket);
        Relay->ListenSocket = -1;
    }
}

void HostChatRelayRun(HostChatRelay *Relay, HostChatRelayLogFunction Log,
                      void *UserData) {
    char Status[80];
    int Index;

    if (Relay == NULL) {
        return;
    }
    Relay->Stop = 0;
    Relay->ListenSocket = HostChatCreateListenSocket(Relay->Port);
    if (Relay->ListenSocket < 0) {
        HostChatRelayLog(Log, UserData, "listen failed");
        return;
    }
    HostChatSetNonBlocking(Relay->ListenSocket);
    snprintf(Status, sizeof(Status), "server *:%u (GUI+Guest connect here)",
             Relay->Port);
    HostChatRelayLog(Log, UserData, Status);

    while (!Relay->Stop) {
        struct pollfd Polls[HOST_CHAT_MAX_CLIENTS + 1];
        int Map[HOST_CHAT_MAX_CLIENTS + 1];
        int Count = 0;
        int Ready;

        Polls[Count].fd = Relay->ListenSocket;
        Polls[Count].events = POLLIN;
        Polls[Count].revents = 0;
        Map[Count] = -1;
        Count++;
        for (Index = 0; Index < HOST_CHAT_MAX_CLIENTS; Index++) {
            if (Relay->Clients[Index].SocketDescriptor < 0) {
                continue;
            }
            Polls[Count].fd = Relay->Clients[Index].SocketDescriptor;
            Polls[Count].events = POLLIN | POLLHUP | POLLERR;
            Polls[Count].revents = 0;
            Map[Count] = Index;
            Count++;
        }
        Ready = poll(Polls, (nfds_t)Count, 300);
        if (Ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (Ready == 0) {
            continue;
        }
        if (Polls[0].revents & POLLIN) {
            char PeerAddress[64];
            int Accepted =
                HostChatAcceptPeer(Relay->ListenSocket, PeerAddress,
                                   sizeof(PeerAddress));

            if (Accepted >= 0) {
                char Note[96];
                int Slot = HostChatRelayAddClient(Relay, Accepted, PeerAddress);

                if (Slot >= 0) {
                    snprintf(Note, sizeof(Note), "accept %s", PeerAddress);
                    HostChatRelayLog(Log, UserData, Note);
                }
            }
        }
        for (Index = 1; Index < Count; Index++) {
            int ClientIndex;
            char Buffer[512];
            ssize_t Got;

            if (Polls[Index].revents == 0) {
                continue;
            }
            ClientIndex = Map[Index];
            if (Polls[Index].revents & (POLLHUP | POLLERR)) {
                HostChatRelayDropClient(Relay, ClientIndex);
                continue;
            }
            Got = HostChatSocketRead(Relay->Clients[ClientIndex].SocketDescriptor,
                                     Buffer, sizeof(Buffer));
            if (Got <= 0) {
                HostChatRelayDropClient(Relay, ClientIndex);
                continue;
            }
            HostChatRelayFeed(Relay, ClientIndex, Buffer, Got);
        }
    }

    for (Index = 0; Index < HOST_CHAT_MAX_CLIENTS; Index++) {
        HostChatRelayDropClient(Relay, Index);
    }
    HostChatCloseSocket(Relay->ListenSocket);
    Relay->ListenSocket = -1;
    HostChatRelayLog(Log, UserData, "server stopped");
}
