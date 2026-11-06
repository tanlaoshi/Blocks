/*
 * Conversation.h — 会话与消息模型（左侧列表 / 右侧内容）
 * Identifier 与中继 +PEER / MSG / SEND 的 peer id 一致
 */
#ifndef HOST_CHAT_CONVERSATION_H
#define HOST_CHAT_CONVERSATION_H

#include <glib.h>

typedef struct HostChatMessage {
    int FromPeer;
    char *Text;
} HostChatMessage;

typedef struct HostChatConversation {
    guint Identifier;
    char Title[64];
    char PeerAddress[64];
    GPtrArray *Messages;
    int Closed;
} HostChatConversation;

HostChatConversation *HostChatConversationCreate(guint Identifier,
                                                 const char *PeerAddress);
void HostChatConversationDestroy(HostChatConversation *Conversation);
void HostChatConversationAppend(HostChatConversation *Conversation, int FromPeer,
                                const char *Text);

#endif
