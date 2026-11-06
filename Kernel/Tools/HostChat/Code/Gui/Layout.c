/*
 * Layout.c — 微信式布局：左会话 / 右上消息 / 右下发送
 */
#include "Widgets.h"

#include "Worker.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void HostChatGuiOnListenClicked(GtkButton *Button, gpointer UserData) {
    HostChatGuiWidgets *Widgets = (HostChatGuiWidgets *)UserData;
    const char *PortText;
    unsigned long PortValue;

    (void)Button;
    PortText = gtk_editable_get_text(GTK_EDITABLE(Widgets->PortEntry));
    PortValue = strtoul(PortText != NULL ? PortText : "9090", NULL, 10);
    if (PortValue == 0 || PortValue > 65535ul) {
        HostChatGuiAppendStatus(Widgets, "端口无效");
        return;
    }
    Widgets->ListenPort = (unsigned short)PortValue;
    HostChatGuiWorkerStartListen(Widgets);
}

static void HostChatGuiOnStopClicked(GtkButton *Button, gpointer UserData) {
    (void)Button;
    HostChatGuiWorkerRequestStop((HostChatGuiWidgets *)UserData);
}

static void HostChatGuiOnSendClicked(GtkButton *Button, gpointer UserData) {
    HostChatGuiWidgets *Widgets = (HostChatGuiWidgets *)UserData;
    const char *Text;

    (void)Button;
    Text = gtk_editable_get_text(GTK_EDITABLE(Widgets->ComposeEntry));
    if (Text == NULL || Text[0] == '\0') {
        return;
    }
    if (HostChatGuiSendToActive(Widgets, Text) == 0) {
        gtk_editable_set_text(GTK_EDITABLE(Widgets->ComposeEntry), "");
    }
}

static void HostChatGuiOnComposeActivate(GtkEntry *Entry, gpointer UserData) {
    (void)Entry;
    HostChatGuiOnSendClicked(NULL, UserData);
}

static void HostChatGuiOnConversationRow(GtkListBox *ListBox, GtkListBoxRow *Row,
                                         gpointer UserData) {
    HostChatGuiWidgets *Widgets = (HostChatGuiWidgets *)UserData;
    HostChatConversation *Conversation;

    (void)ListBox;
    if (Row == NULL) {
        return;
    }
    Conversation = (HostChatConversation *)g_object_get_data(G_OBJECT(Row), "conversation");
    HostChatGuiShowConversation(Widgets, Conversation);
}

HostChatConversation *HostChatGuiFindConversation(HostChatGuiWidgets *Widgets,
                                                  guint Identifier) {
    guint Index;

    if (Widgets == NULL || Widgets->Conversations == NULL) {
        return NULL;
    }
    for (Index = 0; Index < Widgets->Conversations->len; Index++) {
        HostChatConversation *Conversation =
            g_ptr_array_index(Widgets->Conversations, Index);

        if (Conversation->Identifier == Identifier) {
            return Conversation;
        }
    }
    return NULL;
}

void HostChatGuiAppendStatus(HostChatGuiWidgets *Widgets, const char *Text) {
    if (Widgets == NULL || Widgets->StatusLabel == NULL) {
        return;
    }
    gtk_label_set_text(Widgets->StatusLabel, Text != NULL ? Text : "");
}

void HostChatGuiReloadMessages(HostChatGuiWidgets *Widgets) {
    HostChatConversation *Conversation;
    GtkTextBuffer *Buffer;
    guint Index;

    if (Widgets == NULL || Widgets->MessageBuffer == NULL) {
        return;
    }
    Buffer = Widgets->MessageBuffer;
    gtk_text_buffer_set_text(Buffer, "", -1);
    Conversation = Widgets->ActiveConversation;
    if (Conversation == NULL) {
        return;
    }
    for (Index = 0; Index < Conversation->Messages->len; Index++) {
        HostChatMessage *Message = g_ptr_array_index(Conversation->Messages, Index);
        GtkTextIter End;
        char Line[1024];

        snprintf(Line, sizeof(Line), "%s%s\n", Message->FromPeer ? "对方: " : "我: ",
                 Message->Text != NULL ? Message->Text : "");
        gtk_text_buffer_get_end_iter(Buffer, &End);
        gtk_text_buffer_insert(Buffer, &End, Line, -1);
    }
}

void HostChatGuiShowConversation(HostChatGuiWidgets *Widgets,
                                 HostChatConversation *Conversation) {
    char Title[96];

    Widgets->ActiveConversation = Conversation;
    if (Conversation == NULL) {
        gtk_label_set_text(Widgets->ChatTitleLabel, "选择左侧会话");
        gtk_text_buffer_set_text(Widgets->MessageBuffer, "", -1);
        gtk_widget_set_sensitive(GTK_WIDGET(Widgets->ComposeEntry), FALSE);
        gtk_widget_set_sensitive(GTK_WIDGET(Widgets->SendButton), FALSE);
        return;
    }
    snprintf(Title, sizeof(Title), "%s%s", Conversation->Title,
             Conversation->Closed ? "（已断开）" : "");
    gtk_label_set_text(Widgets->ChatTitleLabel, Title);
    HostChatGuiReloadMessages(Widgets);
    gtk_widget_set_sensitive(GTK_WIDGET(Widgets->ComposeEntry), !Conversation->Closed);
    gtk_widget_set_sensitive(GTK_WIDGET(Widgets->SendButton), !Conversation->Closed);
}

void HostChatGuiRefreshConversationList(HostChatGuiWidgets *Widgets) {
    GtkWidget *Child;
    guint Index;

    while ((Child = gtk_widget_get_first_child(GTK_WIDGET(Widgets->ConversationList))) !=
           NULL) {
        gtk_list_box_remove(Widgets->ConversationList, Child);
    }
    for (Index = 0; Index < Widgets->Conversations->len; Index++) {
        HostChatConversation *Conversation =
            g_ptr_array_index(Widgets->Conversations, Index);
        GtkWidget *Row;
        GtkWidget *Label;
        char Caption[96];

        snprintf(Caption, sizeof(Caption), "%s%s", Conversation->Title,
                 Conversation->Closed ? " · 断" : "");
        Label = gtk_label_new(Caption);
        gtk_label_set_xalign(GTK_LABEL(Label), 0.0f);
        gtk_widget_set_margin_top(Label, 8);
        gtk_widget_set_margin_bottom(Label, 8);
        gtk_widget_set_margin_start(Label, 10);
        gtk_widget_set_margin_end(Label, 10);
        Row = gtk_list_box_row_new();
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(Row), Label);
        g_object_set_data(G_OBJECT(Row), "conversation", Conversation);
        gtk_list_box_append(Widgets->ConversationList, Row);
        if (Conversation == Widgets->ActiveConversation) {
            gtk_list_box_select_row(Widgets->ConversationList, GTK_LIST_BOX_ROW(Row));
        }
    }
}

GtkWidget *HostChatGuiBuildWindow(HostChatGuiWidgets *Widgets) {
    GtkWidget *Window;
    GtkWidget *Root;
    GtkWidget *Header;
    GtkWidget *Body;
    GtkWidget *Sidebar;
    GtkWidget *SideScroll;
    GtkWidget *ChatColumn;
    GtkWidget *MessageScroll;
    GtkWidget *ComposeRow;
    GtkWidget *PortLabel;

    Window = gtk_application_window_new(Widgets->Application);
    Widgets->Window = GTK_WINDOW(Window);
    gtk_window_set_title(Widgets->Window, "HostChat");
    gtk_window_set_default_size(Widgets->Window, 880, 560);

    Root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(Widgets->Window, Root);

    Header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_top(Header, 10);
    gtk_widget_set_margin_bottom(Header, 8);
    gtk_widget_set_margin_start(Header, 12);
    gtk_widget_set_margin_end(Header, 12);
    PortLabel = gtk_label_new("端口");
    gtk_box_append(GTK_BOX(Header), PortLabel);
    Widgets->PortEntry = GTK_ENTRY(gtk_entry_new());
    gtk_editable_set_text(GTK_EDITABLE(Widgets->PortEntry), "9090");
    gtk_widget_set_size_request(GTK_WIDGET(Widgets->PortEntry), 80, -1);
    gtk_box_append(GTK_BOX(Header), GTK_WIDGET(Widgets->PortEntry));
    Widgets->ListenButton = GTK_BUTTON(gtk_button_new_with_label("启动服务器"));
    Widgets->StopButton = GTK_BUTTON(gtk_button_new_with_label("停止"));
    gtk_widget_set_sensitive(GTK_WIDGET(Widgets->StopButton), FALSE);
    g_signal_connect(Widgets->ListenButton, "clicked", G_CALLBACK(HostChatGuiOnListenClicked),
                     Widgets);
    g_signal_connect(Widgets->StopButton, "clicked", G_CALLBACK(HostChatGuiOnStopClicked),
                     Widgets);
    gtk_box_append(GTK_BOX(Header), GTK_WIDGET(Widgets->ListenButton));
    gtk_box_append(GTK_BOX(Header), GTK_WIDGET(Widgets->StopButton));
    Widgets->StatusLabel = GTK_LABEL(gtk_label_new("服务器未启动"));
    gtk_label_set_xalign(Widgets->StatusLabel, 0.0f);
    gtk_widget_set_hexpand(GTK_WIDGET(Widgets->StatusLabel), TRUE);
    gtk_box_append(GTK_BOX(Header), GTK_WIDGET(Widgets->StatusLabel));
    gtk_box_append(GTK_BOX(Root), Header);

    Body = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_vexpand(Body, TRUE);
    gtk_box_append(GTK_BOX(Root), Body);

    Sidebar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(Sidebar, 220, -1);
    gtk_widget_add_css_class(Sidebar, "sidebar");
    {
        GtkWidget *SideTitle = gtk_label_new("会话");
        gtk_widget_set_margin_top(SideTitle, 8);
        gtk_widget_set_margin_bottom(SideTitle, 4);
        gtk_widget_set_margin_start(SideTitle, 12);
        gtk_label_set_xalign(GTK_LABEL(SideTitle), 0.0f);
        gtk_box_append(GTK_BOX(Sidebar), SideTitle);
    }
    SideScroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(SideScroll, TRUE);
    gtk_widget_set_hexpand(SideScroll, FALSE);
    Widgets->ConversationList = GTK_LIST_BOX(gtk_list_box_new());
    gtk_list_box_set_selection_mode(Widgets->ConversationList, GTK_SELECTION_SINGLE);
    g_signal_connect(Widgets->ConversationList, "row-activated",
                     G_CALLBACK(HostChatGuiOnConversationRow), Widgets);
    g_signal_connect(Widgets->ConversationList, "row-selected",
                     G_CALLBACK(HostChatGuiOnConversationRow), Widgets);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(SideScroll),
                                  GTK_WIDGET(Widgets->ConversationList));
    gtk_box_append(GTK_BOX(Sidebar), SideScroll);
    gtk_box_append(GTK_BOX(Body), Sidebar);

    ChatColumn = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(ChatColumn, TRUE);
    gtk_widget_set_vexpand(ChatColumn, TRUE);
    Widgets->ChatTitleLabel = GTK_LABEL(gtk_label_new("选择左侧会话"));
    gtk_label_set_xalign(Widgets->ChatTitleLabel, 0.0f);
    gtk_widget_set_margin_top(GTK_WIDGET(Widgets->ChatTitleLabel), 10);
    gtk_widget_set_margin_bottom(GTK_WIDGET(Widgets->ChatTitleLabel), 6);
    gtk_widget_set_margin_start(GTK_WIDGET(Widgets->ChatTitleLabel), 12);
    gtk_box_append(GTK_BOX(ChatColumn), GTK_WIDGET(Widgets->ChatTitleLabel));

    MessageScroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(MessageScroll, TRUE);
    gtk_widget_set_hexpand(MessageScroll, TRUE);
    Widgets->MessageView = GTK_TEXT_VIEW(gtk_text_view_new());
    Widgets->MessageBuffer = gtk_text_view_get_buffer(Widgets->MessageView);
    gtk_text_view_set_editable(Widgets->MessageView, FALSE);
    gtk_text_view_set_cursor_visible(Widgets->MessageView, FALSE);
    gtk_text_view_set_wrap_mode(Widgets->MessageView, GTK_WRAP_WORD_CHAR);
    gtk_widget_set_margin_start(GTK_WIDGET(Widgets->MessageView), 8);
    gtk_widget_set_margin_end(GTK_WIDGET(Widgets->MessageView), 8);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(MessageScroll),
                                  GTK_WIDGET(Widgets->MessageView));
    gtk_box_append(GTK_BOX(ChatColumn), MessageScroll);

    ComposeRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_top(ComposeRow, 8);
    gtk_widget_set_margin_bottom(ComposeRow, 12);
    gtk_widget_set_margin_start(ComposeRow, 12);
    gtk_widget_set_margin_end(ComposeRow, 12);
    Widgets->ComposeEntry = GTK_ENTRY(gtk_entry_new());
    gtk_widget_set_hexpand(GTK_WIDGET(Widgets->ComposeEntry), TRUE);
    gtk_widget_set_sensitive(GTK_WIDGET(Widgets->ComposeEntry), FALSE);
    g_signal_connect(Widgets->ComposeEntry, "activate",
                     G_CALLBACK(HostChatGuiOnComposeActivate), Widgets);
    Widgets->SendButton = GTK_BUTTON(gtk_button_new_with_label("发送"));
    gtk_widget_set_sensitive(GTK_WIDGET(Widgets->SendButton), FALSE);
    g_signal_connect(Widgets->SendButton, "clicked", G_CALLBACK(HostChatGuiOnSendClicked),
                     Widgets);
    gtk_box_append(GTK_BOX(ComposeRow), GTK_WIDGET(Widgets->ComposeEntry));
    gtk_box_append(GTK_BOX(ComposeRow), GTK_WIDGET(Widgets->SendButton));
    gtk_box_append(GTK_BOX(ChatColumn), ComposeRow);

    gtk_box_append(GTK_BOX(Body), ChatColumn);
    return Window;
}
