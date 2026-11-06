/*
 * Protocol.h — GUI 客户端中继下行解析
 */
#ifndef HOST_CHAT_PROTOCOL_H
#define HOST_CHAT_PROTOCOL_H

#include "Widgets.h"

void HostChatHandleGuiProtocolLine(HostChatGuiWidgets *Widgets, const char *Line);

#endif
