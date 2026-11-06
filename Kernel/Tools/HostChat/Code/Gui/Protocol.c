/*
 * Protocol.c — GUI 客户端解析中继下行（+PEER / -PEER / MSG）
 */
#include "Widgets.h"

#include <stdio.h>
#include <string.h>

typedef struct HostChatIdlePeerJoined {
    HostChatGuiWidgets *Widgets;
    HostChatConversation *Conversation;
} HostChatIdlePeerJoined;

typedef struct HostChatIdlePeerMessage {
    HostChatGuiWidgets *Widgets;
    unsigned Identifier;
    char *Text;
} HostChatIdlePeerMessage;

typedef struct HostChatIdlePeerClosed {
    HostChatGuiWidgets *Widgets;
    HostChatConversation *Conversation;
} HostChatIdlePeerClosed;

static gboolean HostChatIdleOnPeerJoined(gpointer UserData) {
    HostChatIdlePeerJoined *Item = (HostChatIdlePeerJoined *)UserData;

    g_ptr_array_add(Item->Widgets->Conversations, Item->Conversation);
    HostChatGuiRefreshConversationList(Item->Widgets);
    HostChatGuiShowConversation(Item->Widgets, Item->Conversation);
    HostChatGuiAppendStatus(Item->Widgets, "Guest 已接入");
    g_free(Item);
    return G_SOURCE_REMOVE;
}

static gboolean HostChatIdleOnPeerMessage(gpointer UserData) {
    HostChatIdlePeerMessage *Item = (HostChatIdlePeerMessage *)UserData;
    HostChatConversation *Conversation;

    /*
     * 须在 idle 里再 Find：Guest 首行会连发 +PEER 与 MSG，
     * 若在读线程里 Find，+PEER 的 idle 尚未跑 → 会话不存在 → 首句被丢。
     */
    Conversation = HostChatGuiFindConversation(Item->Widgets, Item->Identifier);
    if (Conversation != NULL) {
        HostChatConversationAppend(Conversation, 1, Item->Text);
        if (Item->Widgets->ActiveConversation == Conversation) {
            HostChatGuiReloadMessages(Item->Widgets);
        }
    }
    g_free(Item->Text);
    g_free(Item);
    return G_SOURCE_REMOVE;
}

static gboolean HostChatIdleOnPeerClosed(gpointer UserData) {
    HostChatIdlePeerClosed *Item = (HostChatIdlePeerClosed *)UserData;

    Item->Conversation->Closed = 1;
    HostChatGuiRefreshConversationList(Item->Widgets);
    if (Item->Widgets->ActiveConversation == Item->Conversation) {
        HostChatGuiShowConversation(Item->Widgets, Item->Conversation);
    }
    HostChatGuiAppendStatus(Item->Widgets, "Guest 断开");
    g_free(Item);
    return G_SOURCE_REMOVE;
}

void HostChatHandleGuiProtocolLine(HostChatGuiWidgets *Widgets, const char *Line) {
    unsigned Identifier = 0;
    char Address[64];
    const char *Text;
    HostChatConversation *Conversation;

    if (strncmp(Line, "+PEER ", 6) == 0) {
        HostChatIdlePeerJoined *Joined;

        if (sscanf(Line + 6, "%u %63s", &Identifier, Address) < 1) {
            return;
        }
        if (HostChatGuiFindConversation(Widgets, Identifier) != NULL) {
            return;
        }
        Conversation = HostChatConversationCreate(Identifier, Address);
        Joined = g_new0(HostChatIdlePeerJoined, 1);
        Joined->Widgets = Widgets;
        Joined->Conversation = Conversation;
        g_idle_add(HostChatIdleOnPeerJoined, Joined);
        return;
    }
    if (strncmp(Line, "-PEER ", 6) == 0) {
        HostChatIdlePeerClosed *Closed;

        if (sscanf(Line + 6, "%u", &Identifier) != 1) {
            return;
        }
        Conversation = HostChatGuiFindConversation(Widgets, Identifier);
        if (Conversation == NULL || Conversation->Closed) {
            return;
        }
        Closed = g_new0(HostChatIdlePeerClosed, 1);
        Closed->Widgets = Widgets;
        Closed->Conversation = Conversation;
        g_idle_add(HostChatIdleOnPeerClosed, Closed);
        return;
    }
    if (strncmp(Line, "MSG ", 4) != 0) {
        return;
    }
    if (sscanf(Line + 4, "%u", &Identifier) != 1) {
        return;
    }
    Text = Line + 4;
    while (*Text != '\0' && *Text != ' ') {
        Text++;
    }
    while (*Text == ' ') {
        Text++;
    }
    if (Text[0] == '\0') {
        return;
    }
    {
        HostChatIdlePeerMessage *Item = g_new0(HostChatIdlePeerMessage, 1);

        Item->Widgets = Widgets;
        Item->Identifier = Identifier;
        Item->Text = g_strdup(Text);
        g_idle_add(HostChatIdleOnPeerMessage, Item);
    }
}
