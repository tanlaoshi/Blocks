/*
 * Widgets.h — HostChat GUI 控件与窗体状态
 * 本机起中继服务器；GUI 作为客户端连 127.0.0.1
 */
#ifndef HOST_CHAT_WIDGETS_H
#define HOST_CHAT_WIDGETS_H

#include "Conversation.h"
#include "Relay.h"

#include <gtk/gtk.h>

typedef struct HostChatGuiWidgets {
    GtkApplication *Application;
    GtkWindow *Window;

    GtkEntry *PortEntry;
    GtkButton *ListenButton;
    GtkButton *StopButton;
    GtkLabel *StatusLabel;

    GtkListBox *ConversationList;
    GtkLabel *ChatTitleLabel;
    GtkTextView *MessageView;
    GtkTextBuffer *MessageBuffer;
    GtkEntry *ComposeEntry;
    GtkButton *SendButton;

    GPtrArray *Conversations;
    HostChatConversation *ActiveConversation;

    GThread *RelayThread;
    GThread *ClientThread;
    GMutex ConnectionMutex;
    HostChatRelay *Relay;
    int GuiClientSocket;
    volatile int CancelRequested;
    volatile int Listening;
    unsigned short ListenPort;
} HostChatGuiWidgets;

GtkWidget *HostChatGuiBuildWindow(HostChatGuiWidgets *Widgets);
void HostChatGuiAppendStatus(HostChatGuiWidgets *Widgets, const char *Text);
void HostChatGuiRefreshConversationList(HostChatGuiWidgets *Widgets);
void HostChatGuiShowConversation(HostChatGuiWidgets *Widgets,
                                 HostChatConversation *Conversation);
void HostChatGuiReloadMessages(HostChatGuiWidgets *Widgets);
HostChatConversation *HostChatGuiFindConversation(HostChatGuiWidgets *Widgets,
                                                  guint Identifier);

#endif
