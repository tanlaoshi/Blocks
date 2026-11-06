/*
 * Worker.c — 本机中继线程 + GUI 客户端泵；发送走 SEND id
 */
#include "Worker.h"

#include "Client.h"
#include "Protocol.h"
#include "Relay.h"
#include "Serve.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

typedef struct HostChatIdleStatus {
    HostChatGuiWidgets *Widgets;
    char *Text;
} HostChatIdleStatus;

static gboolean HostChatIdleSetStatus(gpointer UserData) {
    HostChatIdleStatus *Item = (HostChatIdleStatus *)UserData;

    HostChatGuiAppendStatus(Item->Widgets, Item->Text);
    g_free(Item->Text);
    g_free(Item);
    return G_SOURCE_REMOVE;
}

static void HostChatGuiWorkerJoinThreads(HostChatGuiWidgets *Widgets) {
    if (Widgets->ClientThread != NULL) {
        g_thread_join(Widgets->ClientThread);
        Widgets->ClientThread = NULL;
    }
    if (Widgets->RelayThread != NULL) {
        g_thread_join(Widgets->RelayThread);
        Widgets->RelayThread = NULL;
    }
    if (Widgets->Relay != NULL) {
        HostChatRelayDestroy(Widgets->Relay);
        Widgets->Relay = NULL;
    }
    Widgets->Listening = 0;
    if (Widgets->ListenButton != NULL) {
        gtk_widget_set_sensitive(GTK_WIDGET(Widgets->ListenButton), TRUE);
        gtk_widget_set_sensitive(GTK_WIDGET(Widgets->StopButton), FALSE);
        gtk_widget_set_sensitive(GTK_WIDGET(Widgets->PortEntry), TRUE);
    }
}

static gboolean HostChatIdleJoinWorkers(gpointer UserData) {
    HostChatGuiWidgets *Widgets = (HostChatGuiWidgets *)UserData;

    if (Widgets->ClientThread == NULL && Widgets->RelayThread == NULL &&
        Widgets->Relay == NULL) {
        return G_SOURCE_REMOVE;
    }
    HostChatGuiWorkerJoinThreads(Widgets);
    HostChatGuiAppendStatus(Widgets, "服务器已停止");
    return G_SOURCE_REMOVE;
}

static void HostChatPostStatus(HostChatGuiWidgets *Widgets, const char *Text) {
    HostChatIdleStatus *Item = g_new0(HostChatIdleStatus, 1);

    Item->Widgets = Widgets;
    Item->Text = g_strdup(Text);
    g_idle_add(HostChatIdleSetStatus, Item);
}

static void HostChatRelayLogToGui(const char *Text, void *UserData) {
    HostChatPostStatus((HostChatGuiWidgets *)UserData, Text);
}

static gpointer HostChatRelayThreadMain(gpointer UserData) {
    HostChatGuiWidgets *Widgets = (HostChatGuiWidgets *)UserData;

    HostChatRelayRun(Widgets->Relay, HostChatRelayLogToGui, Widgets);
    return NULL;
}

static gpointer HostChatClientThreadMain(gpointer UserData) {
    HostChatGuiWidgets *Widgets = (HostChatGuiWidgets *)UserData;
    int SocketDescriptor = -1;
    char Line[600];
    size_t LineLength = 0;
    int Attempt;

    for (Attempt = 0; Attempt < 50 && !Widgets->CancelRequested; Attempt++) {
        SocketDescriptor = HostChatClientConnect("127.0.0.1", Widgets->ListenPort);
        if (SocketDescriptor >= 0) {
            break;
        }
        g_usleep(100 * 1000);
    }
    if (SocketDescriptor < 0) {
        HostChatPostStatus(Widgets, "本机 GUI 连服务器失败");
        HostChatGuiWorkerRequestStop(Widgets);
        g_idle_add(HostChatIdleJoinWorkers, Widgets);
        return NULL;
    }
    g_mutex_lock(&Widgets->ConnectionMutex);
    Widgets->GuiClientSocket = SocketDescriptor;
    g_mutex_unlock(&Widgets->ConnectionMutex);
    HostChatPostStatus(Widgets, "本机客户端已连上服务器");

    while (!Widgets->CancelRequested) {
        struct pollfd PollItem;
        char Buffer[512];
        int Ready;
        int Got;
        int Index;

        PollItem.fd = SocketDescriptor;
        PollItem.events = POLLIN | POLLHUP | POLLERR;
        PollItem.revents = 0;
        Ready = poll(&PollItem, 1, 200);
        if (Ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (Ready == 0) {
            continue;
        }
        if (PollItem.revents & (POLLHUP | POLLERR)) {
            break;
        }
        Got = HostChatClientReadAvailable(SocketDescriptor, Buffer, sizeof(Buffer));
        if (Got < 0) {
            break;
        }
        if (Got == 0) {
            continue;
        }
        for (Index = 0; Index < Got; Index++) {
            char Byte = Buffer[Index];

            if (Byte == '\r') {
                continue;
            }
            if (Byte == '\n' || LineLength + 1 >= sizeof(Line)) {
                Line[LineLength] = '\0';
                if (LineLength > 0) {
                    HostChatHandleGuiProtocolLine(Widgets, Line);
                }
                LineLength = 0;
                continue;
            }
            Line[LineLength++] = Byte;
        }
    }

    g_mutex_lock(&Widgets->ConnectionMutex);
    HostChatCloseSocket(Widgets->GuiClientSocket);
    Widgets->GuiClientSocket = -1;
    g_mutex_unlock(&Widgets->ConnectionMutex);
    HostChatGuiWorkerRequestStop(Widgets);
    g_idle_add(HostChatIdleJoinWorkers, Widgets);
    return NULL;
}

void HostChatGuiWorkerStartListen(HostChatGuiWidgets *Widgets) {
    int ProbeSocket;

    if (Widgets->Listening) {
        return;
    }
    Widgets->CancelRequested = 0;
    Widgets->Listening = 1;
    gtk_widget_set_sensitive(GTK_WIDGET(Widgets->ListenButton), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(Widgets->StopButton), TRUE);
    gtk_widget_set_sensitive(GTK_WIDGET(Widgets->PortEntry), FALSE);

    /*
     * 若本机已有 `serve chat` / chat-serve，只当 GUI 客户端连上；
     * 否则本进程自起中继（单窗即可）。
     */
    ProbeSocket = HostChatClientConnect("127.0.0.1", Widgets->ListenPort);
    if (ProbeSocket >= 0) {
        HostChatCloseSocket(ProbeSocket);
        HostChatGuiAppendStatus(Widgets, "接入已有本机服务器");
        Widgets->Relay = NULL;
        Widgets->RelayThread = NULL;
        Widgets->ClientThread =
            g_thread_new("hostchat-client", HostChatClientThreadMain, Widgets);
        return;
    }

    Widgets->Relay = HostChatRelayCreate(Widgets->ListenPort);
    if (Widgets->Relay == NULL) {
        Widgets->Listening = 0;
        gtk_widget_set_sensitive(GTK_WIDGET(Widgets->ListenButton), TRUE);
        gtk_widget_set_sensitive(GTK_WIDGET(Widgets->StopButton), FALSE);
        gtk_widget_set_sensitive(GTK_WIDGET(Widgets->PortEntry), TRUE);
        HostChatGuiAppendStatus(Widgets, "无法创建服务器");
        return;
    }
    Widgets->RelayThread =
        g_thread_new("hostchat-relay", HostChatRelayThreadMain, Widgets);
    Widgets->ClientThread =
        g_thread_new("hostchat-client", HostChatClientThreadMain, Widgets);
}

void HostChatGuiWorkerRequestStop(HostChatGuiWidgets *Widgets) {
    Widgets->CancelRequested = 1;
    g_mutex_lock(&Widgets->ConnectionMutex);
    HostChatCloseSocket(Widgets->GuiClientSocket);
    Widgets->GuiClientSocket = -1;
    if (Widgets->Relay != NULL) {
        HostChatRelayRequestStop(Widgets->Relay);
    }
    g_mutex_unlock(&Widgets->ConnectionMutex);
}

void HostChatGuiWorkerShutdown(HostChatGuiWidgets *Widgets) {
    HostChatGuiWorkerRequestStop(Widgets);
    HostChatGuiWorkerJoinThreads(Widgets);
}

int HostChatGuiSendToActive(HostChatGuiWidgets *Widgets, const char *Text) {
    HostChatConversation *Conversation;
    char Outgoing[560];
    int SocketDescriptor;
    int Result;

    Conversation = Widgets->ActiveConversation;
    if (Conversation == NULL || Conversation->Closed) {
        HostChatGuiAppendStatus(Widgets, "当前会话不可发送");
        return -1;
    }
    snprintf(Outgoing, sizeof(Outgoing), "SEND %u %s\n", Conversation->Identifier,
             Text);
    g_mutex_lock(&Widgets->ConnectionMutex);
    SocketDescriptor = Widgets->GuiClientSocket;
    Result = HostChatClientSendLine(SocketDescriptor, Outgoing);
    g_mutex_unlock(&Widgets->ConnectionMutex);
    if (Result != 0) {
        HostChatGuiAppendStatus(Widgets, "发送失败");
        return -1;
    }
    HostChatConversationAppend(Conversation, 0, Text);
    HostChatGuiReloadMessages(Widgets);
    return 0;
}
