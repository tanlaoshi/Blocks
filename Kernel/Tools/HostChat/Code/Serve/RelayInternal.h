/*
 * RelayInternal.h — 中继内部结构（Relay.c / RelaySession.c）
 */
#ifndef HOST_CHAT_RELAY_INTERNAL_H
#define HOST_CHAT_RELAY_INTERNAL_H

#include "Relay.h"

#include <stddef.h>
#include <sys/types.h>

typedef struct HostChatRelayClient {
    int SocketDescriptor;
    int IsGui;
    unsigned Identifier;
    char PeerAddress[64];
    char Line[512];
    size_t LineLength;
} HostChatRelayClient;

struct HostChatRelay {
    unsigned short Port;
    volatile int Stop;
    int ListenSocket;
    HostChatRelayClient Clients[HOST_CHAT_MAX_CLIENTS];
    unsigned NextPeerIdentifier;
};

void HostChatRelayDropClient(HostChatRelay *Relay, int Index);
int HostChatRelayAddClient(HostChatRelay *Relay, int SocketDescriptor,
                           const char *PeerAddress);
void HostChatRelayFeed(HostChatRelay *Relay, int Index, const char *Data,
                       ssize_t Count);

#endif
