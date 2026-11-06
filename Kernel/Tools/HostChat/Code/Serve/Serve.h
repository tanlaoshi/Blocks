/*
 * Serve.h — HostChat TCP listen / accept / 读写（无 GTK）
 */
#ifndef HOST_CHAT_SERVE_H
#define HOST_CHAT_SERVE_H

#include <stddef.h>
#include <sys/types.h>

#define HOST_CHAT_DEFAULT_PORT 9090u

int HostChatCreateListenSocket(unsigned short Port);
int HostChatAcceptPeer(int ListenSocket, char *PeerAddress, size_t PeerAddressBytes);
int HostChatConnectPeer(const char *Host, unsigned short Port);
int HostChatSetNonBlocking(int SocketDescriptor);
ssize_t HostChatSocketRead(int SocketDescriptor, void *Buffer, size_t Length);
ssize_t HostChatSocketWrite(int SocketDescriptor, const void *Buffer, size_t Length);
void HostChatCloseSocket(int SocketDescriptor);

#endif
