/*
 * RelaySession.c — 中继会话：GUI/peer 行协议与投喂
 */
#include "RelayInternal.h"

#include "Serve.h"

#include <stdio.h>
#include <string.h>

static int HostChatRelayWriteAll(int SocketDescriptor, const char *Text) {
    size_t Length = strlen(Text);
    size_t Off = 0;

    while (Off < Length) {
        ssize_t Written =
            HostChatSocketWrite(SocketDescriptor, Text + Off, Length - Off);
        if (Written <= 0) {
            return -1;
        }
        Off += (size_t)Written;
    }
    return 0;
}

static HostChatRelayClient *HostChatRelayFindGui(HostChatRelay *Relay) {
    int Index;

    for (Index = 0; Index < HOST_CHAT_MAX_CLIENTS; Index++) {
        if (Relay->Clients[Index].SocketDescriptor >= 0 &&
            Relay->Clients[Index].IsGui) {
            return &Relay->Clients[Index];
        }
    }
    return NULL;
}

static HostChatRelayClient *HostChatRelayFindPeer(HostChatRelay *Relay,
                                                  unsigned Identifier) {
    int Index;

    for (Index = 0; Index < HOST_CHAT_MAX_CLIENTS; Index++) {
        if (Relay->Clients[Index].SocketDescriptor >= 0 &&
            !Relay->Clients[Index].IsGui &&
            Relay->Clients[Index].Identifier == Identifier) {
            return &Relay->Clients[Index];
        }
    }
    return NULL;
}

static void HostChatRelayNotifyGui(HostChatRelay *Relay, const char *Line) {
    HostChatRelayClient *Gui = HostChatRelayFindGui(Relay);

    if (Gui != NULL) {
        (void)HostChatRelayWriteAll(Gui->SocketDescriptor, Line);
    }
}

void HostChatRelayDropClient(HostChatRelay *Relay, int Index) {
    HostChatRelayClient *Client = &Relay->Clients[Index];
    char Note[96];

    if (Client->SocketDescriptor < 0) {
        return;
    }
    if (!Client->IsGui && Client->Identifier != 0) {
        snprintf(Note, sizeof(Note), "-PEER %u\n", Client->Identifier);
        HostChatRelayNotifyGui(Relay, Note);
    }
    HostChatCloseSocket(Client->SocketDescriptor);
    memset(Client, 0, sizeof(*Client));
    Client->SocketDescriptor = -1;
}

static void HostChatRelayHandleGuiLine(HostChatRelay *Relay, const char *Line) {
    unsigned Identifier = 0;
    const char *Text;
    HostChatRelayClient *Peer;
    char Out[560];

    if (strncmp(Line, "SEND ", 5) != 0) {
        return;
    }
    if (Line[5] == '*') {
        int Index;

        Text = Line + 6;
        while (*Text == ' ') {
            Text++;
        }
        snprintf(Out, sizeof(Out), "%s\n", Text);
        for (Index = 0; Index < HOST_CHAT_MAX_CLIENTS; Index++) {
            if (Relay->Clients[Index].SocketDescriptor >= 0 &&
                !Relay->Clients[Index].IsGui) {
                (void)HostChatRelayWriteAll(Relay->Clients[Index].SocketDescriptor,
                                            Out);
            }
        }
        return;
    }
    if (sscanf(Line + 5, "%u", &Identifier) != 1) {
        return;
    }
    Text = Line + 5;
    while (*Text != '\0' && *Text != ' ') {
        Text++;
    }
    while (*Text == ' ') {
        Text++;
    }
    Peer = HostChatRelayFindPeer(Relay, Identifier);
    if (Peer == NULL) {
        return;
    }
    snprintf(Out, sizeof(Out), "%s\n", Text);
    (void)HostChatRelayWriteAll(Peer->SocketDescriptor, Out);
}

static void HostChatRelayHandlePeerLine(HostChatRelay *Relay,
                                        HostChatRelayClient *Client,
                                        const char *Line) {
    char Out[600];

    snprintf(Out, sizeof(Out), "MSG %u %s\n", Client->Identifier, Line);
    HostChatRelayNotifyGui(Relay, Out);
}

static void HostChatRelayOnLine(HostChatRelay *Relay, int Index) {
    HostChatRelayClient *Client = &Relay->Clients[Index];
    char *Line = Client->Line;

    if (!Client->IsGui && Client->Identifier == 0) {
        if (strcmp(Line, HOST_CHAT_GUI_HELLO) == 0) {
            if (HostChatRelayFindGui(Relay) != NULL) {
                (void)HostChatRelayWriteAll(Client->SocketDescriptor,
                                            "ERR GUI busy\n");
                HostChatRelayDropClient(Relay, Index);
                return;
            }
            Client->IsGui = 1;
            (void)HostChatRelayWriteAll(Client->SocketDescriptor, "OK GUI\n");
            return;
        }
        Client->Identifier = Relay->NextPeerIdentifier++;
        {
            char Note[128];

            snprintf(Note, sizeof(Note), "+PEER %u %s\n", Client->Identifier,
                     Client->PeerAddress);
            HostChatRelayNotifyGui(Relay, Note);
        }
    }
    if (Client->IsGui) {
        HostChatRelayHandleGuiLine(Relay, Line);
    } else {
        HostChatRelayHandlePeerLine(Relay, Client, Line);
    }
}

void HostChatRelayFeed(HostChatRelay *Relay, int Index, const char *Data,
                       ssize_t Count) {
    HostChatRelayClient *Client = &Relay->Clients[Index];
    ssize_t Cursor;

    for (Cursor = 0; Cursor < Count; Cursor++) {
        char Byte = Data[Cursor];

        if (Byte == '\r') {
            continue;
        }
        if (Byte == '\n' || Client->LineLength + 1 >= sizeof(Client->Line)) {
            Client->Line[Client->LineLength] = '\0';
            if (Client->LineLength > 0) {
                HostChatRelayOnLine(Relay, Index);
            }
            Client->LineLength = 0;
            continue;
        }
        Client->Line[Client->LineLength++] = Byte;
    }
}

int HostChatRelayAddClient(HostChatRelay *Relay, int SocketDescriptor,
                           const char *PeerAddress) {
    int Index;

    for (Index = 0; Index < HOST_CHAT_MAX_CLIENTS; Index++) {
        if (Relay->Clients[Index].SocketDescriptor < 0) {
            HostChatRelayClient *Client = &Relay->Clients[Index];

            memset(Client, 0, sizeof(*Client));
            Client->SocketDescriptor = SocketDescriptor;
            snprintf(Client->PeerAddress, sizeof(Client->PeerAddress), "%s",
                     PeerAddress != NULL ? PeerAddress : "?");
            HostChatSetNonBlocking(SocketDescriptor);
            return Index;
        }
    }
    HostChatCloseSocket(SocketDescriptor);
    return -1;
}
