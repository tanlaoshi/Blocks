/*
 * Relay.h — 中心服务器：本机 GUI 客户端 + Guest 等 peer 都连这里
 *
 * Peer（Guest/chatd）：纯文本行（\n 结尾）
 * GUI 客户端：首行 HOSTCHAT-GUI，其后：
 *   下行 +PEER id addr / -PEER id / MSG id text
 *   上行 SEND id text
 */
#ifndef HOST_CHAT_RELAY_H
#define HOST_CHAT_RELAY_H

#include <stddef.h>

#define HOST_CHAT_GUI_HELLO "HOSTCHAT-GUI"
#define HOST_CHAT_MAX_CLIENTS 16

typedef struct HostChatRelay HostChatRelay;

typedef void (*HostChatRelayLogFunction)(const char *Text, void *UserData);

HostChatRelay *HostChatRelayCreate(unsigned short Port);
void HostChatRelayDestroy(HostChatRelay *Relay);
void HostChatRelayRequestStop(HostChatRelay *Relay);
/* 阻塞直到 Stop；在工作线程里调用 */
void HostChatRelayRun(HostChatRelay *Relay, HostChatRelayLogFunction Log,
                      void *UserData);

#endif
