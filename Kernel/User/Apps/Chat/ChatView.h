/*
 * ChatView.h — Guest Chat：左会话 / 右消息（对齐 Files + 宿主绿）
 */
#ifndef CHAT_VIEW_H
#define CHAT_VIEW_H

#define CHAT_LOG_MAXIMUM 10
#define CHAT_LOG_COLUMNS 56
#define CHAT_STATUS_MAXIMUM 72

#define CHAT_WINDOW_WIDTH 740u
#define CHAT_WINDOW_HEIGHT 480u
#define CHAT_SIDE_WIDTH 156u
#define CHAT_PAD 12u
#define CHAT_LINE_HEIGHT 28u
#define CHAT_SEPARATOR_WIDTH 1u
#define CHAT_SESSION_ROW_HEIGHT 48u
#define CHAT_WHO_COLUMN 64u
/* 顶栏：标题 + 状态，白底须盖住两行字 */
#define CHAT_HEADER_HEIGHT 72u
/* 输入框加高；底边与左侧绿栏 CHAT_BODY_BOTTOM 对齐 */
#define CHAT_FIELD_HEIGHT 36u
#define CHAT_BODY_BOTTOM 368u
#define CHAT_COMPOSE_TOP (CHAT_BODY_BOTTOM - CHAT_FIELD_HEIGHT)

void ChatViewSetStatus(const char *Text);
void ChatViewLogPush(const char *Who, const char *Message);
void ChatViewPaint(int WindowId, int Connected);

#endif
