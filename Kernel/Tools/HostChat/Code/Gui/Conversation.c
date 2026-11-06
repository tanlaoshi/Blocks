/*
 * Conversation.c — 会话消息追加
 */
#include "Conversation.h"

#include <stdio.h>
#include <string.h>

static void HostChatMessageFree(gpointer Item) {
    HostChatMessage *Message = (HostChatMessage *)Item;

    if (Message == NULL) {
        return;
    }
    g_free(Message->Text);
    g_free(Message);
}

HostChatConversation *HostChatConversationCreate(guint Identifier,
                                                 const char *PeerAddress) {
    HostChatConversation *Conversation;

    Conversation = g_new0(HostChatConversation, 1);
    Conversation->Identifier = Identifier;
    Conversation->Messages = g_ptr_array_new_with_free_func(HostChatMessageFree);
    if (PeerAddress != NULL && PeerAddress[0] != '\0') {
        snprintf(Conversation->PeerAddress, sizeof(Conversation->PeerAddress), "%s",
                 PeerAddress);
        snprintf(Conversation->Title, sizeof(Conversation->Title), "Guest %u %s",
                 Identifier, PeerAddress);
    } else {
        snprintf(Conversation->Title, sizeof(Conversation->Title), "Guest %u",
                 Identifier);
    }
    return Conversation;
}

void HostChatConversationDestroy(HostChatConversation *Conversation) {
    if (Conversation == NULL) {
        return;
    }
    if (Conversation->Messages != NULL) {
        g_ptr_array_unref(Conversation->Messages);
    }
    g_free(Conversation);
}

void HostChatConversationAppend(HostChatConversation *Conversation, int FromPeer,
                                const char *Text) {
    HostChatMessage *Message;

    if (Conversation == NULL || Text == NULL) {
        return;
    }
    Message = g_new0(HostChatMessage, 1);
    Message->FromPeer = FromPeer ? 1 : 0;
    Message->Text = g_strdup(Text);
    g_ptr_array_add(Conversation->Messages, Message);
}
