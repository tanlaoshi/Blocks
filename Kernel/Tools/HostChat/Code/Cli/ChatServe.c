/*
 * ChatServe.c — 无头中继：本机 GUI / Guest / chatd 都连此处
 * 用法：chat-serve [port]   （默认 9090；Ctrl-C 停）
 */
#include "Relay.h"
#include "Serve.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static HostChatRelay *gRelay = NULL;

static void OnSignal(int SignalNumber) {
    (void)SignalNumber;
    if (gRelay != NULL) {
        HostChatRelayRequestStop(gRelay);
    }
}

static void LogLine(const char *Text, void *UserData) {
    (void)UserData;
    fprintf(stderr, "chat-serve: %s\n", Text != NULL ? Text : "");
}

int main(int ArgumentCount, char **ArgumentVector) {
    unsigned short Port = HOST_CHAT_DEFAULT_PORT;

    if (ArgumentCount >= 2) {
        Port = (unsigned short)atoi(ArgumentVector[1]);
        if (Port == 0) {
            fprintf(stderr, "usage: %s [port]\n", ArgumentVector[0]);
            return 1;
        }
    }

    gRelay = HostChatRelayCreate(Port);
    if (gRelay == NULL) {
        fprintf(stderr, "chat-serve: create failed\n");
        return 1;
    }

    signal(SIGINT, OnSignal);
    signal(SIGTERM, OnSignal);

    fprintf(stderr, "chat-serve: listen *:%u (GUI → 127.0.0.1; Guest → host LAN)\n",
            Port);
    HostChatRelayRun(gRelay, LogLine, NULL);
    HostChatRelayDestroy(gRelay);
    gRelay = NULL;
    fprintf(stderr, "chat-serve: stopped\n");
    return 0;
}
