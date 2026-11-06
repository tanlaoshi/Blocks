#ifndef HOST_CHAT_WORKER_H
#define HOST_CHAT_WORKER_H

#include "Widgets.h"

void HostChatGuiWorkerStartListen(HostChatGuiWidgets *Widgets);
void HostChatGuiWorkerRequestStop(HostChatGuiWidgets *Widgets);
void HostChatGuiWorkerShutdown(HostChatGuiWidgets *Widgets);
int HostChatGuiSendToActive(HostChatGuiWidgets *Widgets, const char *Text);

#endif
