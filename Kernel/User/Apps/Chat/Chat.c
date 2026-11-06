/*
 * Chat.c — Guest 聊天：左会话列表 / 右消息 / 底栏输入框（对齐 Files·宿主）
 */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <Host/UnixStandard.h>
#include <System/Socket.h>
#include <Net/Net.h>
#include <Ui/Ui.h>
#include <Blocks/Thread.h>
#include <Blocks/File.h>
#include "ChatPeer.h"
#include "ChatView.h"

static int WindowId = -1;
static int SocketDescriptor = -1;
static char ReceiveLine[CHAT_LINE_MAXIMUM + 4];
static int ReceiveLength;
static unsigned PeerIp;
static unsigned short PeerPort;

static void Paint(void) {
    /* 只在内容/暴露时整窗重画；打字由 UiApplyKeyToField 只刷输入框 */
    ChatViewPaint(WindowId, SocketDescriptor >= 0);
}

static int TrySend(void) {
    char Draft[UI_TEXT_MAX];

    if (SocketDescriptor < 0 || WindowId < 0) {
        ChatViewSetStatus("未连接：先点「连接」");
        Paint();
        return -1;
    }
    if (UiGetTextField(WindowId, 0, Draft, sizeof(Draft)) != 0 || Draft[0] == 0) {
        return 0;
    }
    if (strcmp(Draft, "/quit") == 0) {
        return -2;
    }
    if (ChatSendLine(SocketDescriptor, Draft, strlen(Draft)) != 0) {
        ChatViewSetStatus("发送失败");
        ChatViewLogPush("--", "send fail");
        Paint();
        return -1;
    }
    ChatViewLogPush("我:", Draft);
    UiSetTextField(WindowId, 0, "");
    ChatViewSetStatus("已发送");
    Paint();
    return 0;
}

static int PollPeer(void) {
    for (;;) {
        char Byte;
        ssize_t Received;

        if (SocketDescriptor < 0) {
            return 0;
        }
        Received = recv(SocketDescriptor, &Byte, 1, MSG_DONTWAIT);
        if (Received < 0) {
            return (errno == EAGAIN) ? 0 : -1;
        }
        if (Received == 0) {
            return -2;
        }
        if (Byte == '\n') {
            if (ReceiveLength > 0 && ReceiveLine[ReceiveLength - 1] == '\r') {
                ReceiveLine[--ReceiveLength] = 0;
            }
            ReceiveLine[ReceiveLength] = 0;
            if (ReceiveLength == 0) {
                continue;
            }
            ChatViewLogPush("对方:", ReceiveLine);
            ReceiveLength = 0;
            ChatViewSetStatus("收到消息");
            Paint();
            return 1;
        }
        if (ReceiveLength + 1 < (int)sizeof(ReceiveLine) &&
            ReceiveLength < CHAT_LINE_MAXIMUM) {
            ReceiveLine[ReceiveLength++] = Byte;
        }
    }
}

static int ConnectPeer(void) {
    char Hint[CHAT_STATUS_MAXIMUM];

    if (SocketDescriptor >= 0) {
        FileClose(SocketDescriptor);
        SocketDescriptor = -1;
    }
    SocketDescriptor = socket(AF_INET, SOCK_STREAM, 0);
    if (SocketDescriptor < 0) {
        ChatViewSetStatus("socket 失败");
        Paint();
        return -1;
    }
    snprintf(Hint, sizeof(Hint), "连接中 %u.%u.%u.%u:%u …",
             (PeerIp >> 24) & 0xffu, (PeerIp >> 16) & 0xffu,
             (PeerIp >> 8) & 0xffu, PeerIp & 0xffu, (unsigned)PeerPort);
    ChatViewSetStatus(Hint);
    Paint();
    ThreadYield();
    if (NetConnect(SocketDescriptor, PeerIp, PeerPort) != 0) {
        snprintf(Hint, sizeof(Hint), "连接失败 %u.%u.%u.%u:%u",
                 (PeerIp >> 24) & 0xffu, (PeerIp >> 16) & 0xffu,
                 (PeerIp >> 8) & 0xffu, PeerIp & 0xffu, (unsigned)PeerPort);
        ChatViewSetStatus(Hint);
        FileClose(SocketDescriptor);
        SocketDescriptor = -1;
        Paint();
        printf("chat: connect fail — 宿主先 serve chat；"
               "chat.peer=宿主局域网:9090\n");
        return -1;
    }
    ChatViewSetStatus("已连接中继，在下方输入后发送");
    Paint();
    return 0;
}

int main(void) {
    int Event;
    char PeerHint[CHAT_STATUS_MAXIMUM];
    unsigned FieldX;
    unsigned FieldWidth;

    if (ChatLoadPeer(&PeerIp, &PeerPort) != 0) {
        return 1;
    }
    printf("chat: peer %u.%u.%u.%u:%u (host relay)\n",
           (PeerIp >> 24) & 0xffu, (PeerIp >> 16) & 0xffu, (PeerIp >> 8) & 0xffu,
           PeerIp & 0xffu, (unsigned)PeerPort);

    if (GraphicsInitialize() != 0) {
        return 1;
    }
    WindowId = UiCreateWindow("Chat", CHAT_WINDOW_WIDTH, CHAT_WINDOW_HEIGHT);
    if (WindowId < 0) {
        return 1;
    }
    if (UiAddButton(WindowId, 0, "连接") != 0 ||
        UiAddButton(WindowId, 1, "发送") != 0) {
        return 1;
    }

    /* 会话行由 ChatView 自绘（通用 List 黑边会在绿底上划穿「Host」） */
    FieldX = CHAT_SIDE_WIDTH + CHAT_SEPARATOR_WIDTH + CHAT_PAD;
    FieldWidth = CHAT_WINDOW_WIDTH - FieldX - CHAT_PAD * 2u;
    if (FieldWidth < 160u) {
        FieldWidth = 160u;
    }
    UiAddTextField(WindowId, 0, FieldX, CHAT_COMPOSE_TOP, FieldWidth,
                   CHAT_FIELD_HEIGHT);
    UiFocusTextField(WindowId, 0);

    snprintf(PeerHint, sizeof(PeerHint), "目标 %u.%u.%u.%u:%u",
             (PeerIp >> 24) & 0xffu, (PeerIp >> 16) & 0xffu,
             (PeerIp >> 8) & 0xffu, PeerIp & 0xffu, (unsigned)PeerPort);
    ChatViewSetStatus(PeerHint);
    Paint();

    for (;;) {
        int Result = PollPeer();

        if (Result == -2) {
            ChatViewSetStatus("对方断开");
            if (SocketDescriptor >= 0) {
                FileClose(SocketDescriptor);
                SocketDescriptor = -1;
            }
            Paint();
        } else if (Result < 0) {
            ChatViewSetStatus("接收失败");
            Paint();
        }
        Event = UiPoll(WindowId);
        if (Event == UI_EVENT_CLOSE || Event < 0) {
            break;
        }
        if (Event == UI_BUTTON_EVENT(0)) {
            (void)ConnectPeer();
        } else if (Event == UI_BUTTON_EVENT(1) ||
                   (UI_IS_KEY(Event) && UI_KEY_CODE(Event) == 0x28)) {
            if (TrySend() == -2) {
                break;
            }
        } else if (Event == UI_EVENT_PAINT) {
            /*
             * 仅内核 PaintUserClient 刷底后（拖动/置顶/Damage）整窗重画。
             * 普通点击/打字不必：点已由控件局部重绘；刷底会再投递 PAINT。
             */
            Paint();
        } else if (UI_IS_KEY(Event) || Event == UI_EVENT_CLICK ||
                   Event == UI_TEXT_EVENT(0)) {
            /* 忽略：键/点输入框已局部刷新；空白点若刷了底会跟 PAINT */
        } else if (Event == UI_EVENT_NONE) {
            ThreadYield();
        }
        usleep(5000);
    }
    if (SocketDescriptor >= 0) {
        FileClose(SocketDescriptor);
    }
    return 0;
}
