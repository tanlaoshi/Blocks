/*
 * Client.h — 本机 GUI 作为客户端连中继服务器（默认 127.0.0.1）
 */
#ifndef HOST_CHAT_CLIENT_H
#define HOST_CHAT_CLIENT_H

#include <stddef.h>

int HostChatClientConnect(const char *Host, unsigned short Port);
int HostChatClientSendLine(int SocketDescriptor, const char *Line);
int HostChatClientReadAvailable(int SocketDescriptor, char *Buffer,
                                size_t Capacity);

#endif
