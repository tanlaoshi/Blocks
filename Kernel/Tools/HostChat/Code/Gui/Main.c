/*
 * Main.c — GTK4 入口（HostChat）
 */
#include "Widgets.h"

#include "Conversation.h"
#include "Worker.h"

#include <gtk/gtk.h>
#include <string.h>

static void HostChatGuiOnActivate(GtkApplication *Application, gpointer UserData) {
    HostChatGuiWidgets *Widgets = (HostChatGuiWidgets *)UserData;
    GtkWidget *Window;

    Widgets->Application = Application;
    Window = HostChatGuiBuildWindow(Widgets);
    gtk_window_present(GTK_WINDOW(Window));
}

static void HostChatGuiOnShutdown(GtkApplication *Application, gpointer UserData) {
    HostChatGuiWidgets *Widgets = (HostChatGuiWidgets *)UserData;
    guint Index;

    (void)Application;
    HostChatGuiWorkerShutdown(Widgets);
    if (Widgets->Conversations != NULL) {
        for (Index = 0; Index < Widgets->Conversations->len; Index++) {
            HostChatConversationDestroy(g_ptr_array_index(Widgets->Conversations, Index));
        }
        g_ptr_array_free(Widgets->Conversations, TRUE);
        Widgets->Conversations = NULL;
    }
    g_mutex_clear(&Widgets->ConnectionMutex);
}

int main(int ArgumentCount, char **ArgumentVector) {
    HostChatGuiWidgets Widgets;
    GtkApplication *Application;
    int Status;

    memset(&Widgets, 0, sizeof(Widgets));
    Widgets.GuiClientSocket = -1;
    Widgets.Conversations = g_ptr_array_new();
    g_mutex_init(&Widgets.ConnectionMutex);

    Application = gtk_application_new("com.blocks.hostchat", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(Application, "activate", G_CALLBACK(HostChatGuiOnActivate), &Widgets);
    g_signal_connect(Application, "shutdown", G_CALLBACK(HostChatGuiOnShutdown), &Widgets);
    Status = g_application_run(G_APPLICATION(Application), ArgumentCount, ArgumentVector);
    g_object_unref(Application);
    return Status;
}
