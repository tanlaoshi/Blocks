/*
 * ChatPeer.h — Guest 作中继 peer：读 chat.peer、行发送
 * 宿主 hostchat-gui 起服务器；本端连 IP:端口，纯文本行（\n）
 */
#ifndef CHAT_PEER_H
#define CHAT_PEER_H

#include <stddef.h>

#define CHAT_LINE_MAXIMUM 200
#define CHAT_PORT_DEFAULT 9090

int ChatLoadPeer(unsigned *OutIp, unsigned short *OutPort);
int ChatSendLine(int SocketDescriptor, const char *Line, size_t Length);

#endif
