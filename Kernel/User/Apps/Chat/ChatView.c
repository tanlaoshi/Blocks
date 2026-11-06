/*
 * ChatView.c — 左会话栏 + 右顶栏/消息框/底输入（自绘；勿套通用 List）
 */
#include "ChatView.h"

#include <string.h>
#include <Graphics/Graphics.h>
#include <Ui/Ui.h>

#define COLOR_SIDE 0x0007C160u
#define COLOR_SIDE_DEEP 0x0005A050u
#define COLOR_SESSION 0x0006A858u
#define COLOR_BODY 0x00E8E8E8u
#define COLOR_PANEL 0x00FFFFFFu
#define COLOR_TITLE 0x00181818u
#define COLOR_MUTED 0x00606060u
#define COLOR_TEXT 0x00202020u
#define COLOR_SIDE_TEXT 0x00FFFFFFu
#define COLOR_BORDER 0x00A0A0A0u
#define COLOR_ME 0x0007A050u
#define COLOR_PEER 0x003060A0u
#define COLOR_DOT_ON 0x00E8FFE8u
#define COLOR_DOT_OFF 0x00B0D0B0u

static char LogLines[CHAT_LOG_MAXIMUM][CHAT_LOG_COLUMNS];
static char LogWho[CHAT_LOG_MAXIMUM][8];
static int LogCount;
static char StatusText[CHAT_STATUS_MAXIMUM];

void ChatViewSetStatus(const char *Text) {
    size_t Index = 0;

    if (Text == NULL) {
        StatusText[0] = 0;
        return;
    }
    while (Text[Index] != 0 && Index + 1 < sizeof(StatusText)) {
        StatusText[Index] = Text[Index];
        Index++;
    }
    StatusText[Index] = 0;
}

void ChatViewLogPush(const char *Who, const char *Message) {
    char Line[CHAT_LOG_COLUMNS];
    int Length = 0;
    int Index = 0;

    if (Who == NULL) {
        Who = "--";
    }
    if (LogCount >= CHAT_LOG_MAXIMUM) {
        for (Index = 1; Index < CHAT_LOG_MAXIMUM; Index++) {
            memcpy(LogLines[Index - 1], LogLines[Index], CHAT_LOG_COLUMNS);
            memcpy(LogWho[Index - 1], LogWho[Index], 8);
        }
        LogCount = CHAT_LOG_MAXIMUM - 1;
    }
    Length = 0;
    while (Who[Length] != 0 && Length < 7) {
        LogWho[LogCount][Length] = Who[Length];
        Length++;
    }
    LogWho[LogCount][Length] = 0;

    Length = 0;
    if (Message != NULL) {
        while (Message[Length] != 0 && Length + 1 < CHAT_LOG_COLUMNS) {
            Line[Length] = Message[Length];
            Length++;
        }
    }
    Line[Length] = 0;
    memcpy(LogLines[LogCount], Line, CHAT_LOG_COLUMNS);
    LogCount++;
}

static void PaintSessionRow(int WindowId, int Connected) {
    unsigned RowY = CHAT_PAD + CHAT_LINE_HEIGHT + 10u;
    unsigned RowX = CHAT_PAD;
    unsigned RowW = CHAT_SIDE_WIDTH - CHAT_PAD * 2u;
    unsigned TextY = RowY + 12u;
    unsigned DotColor = Connected ? COLOR_DOT_ON : COLOR_DOT_OFF;

    (void)GraphicsFillRect(WindowId, RowX, RowY, RowW, CHAT_SESSION_ROW_HEIGHT,
                            COLOR_SESSION);
    (void)GraphicsFillRect(WindowId, RowX, RowY, 4u, CHAT_SESSION_ROW_HEIGHT,
                            COLOR_SIDE_TEXT);
    (void)GraphicsFillRect(WindowId, RowX + 12u, TextY + 8u, 8u, 8u, DotColor);
    (void)GraphicsDrawString(WindowId, RowX + 28u, TextY, COLOR_SIDE_TEXT, "Host");
}

void ChatViewPaint(int WindowId, int Connected) {
    unsigned SideWidth = CHAT_SIDE_WIDTH;
    unsigned MainX = SideWidth + CHAT_SEPARATOR_WIDTH;
    unsigned MainWidth;
    unsigned PanelX;
    unsigned PanelW;
    unsigned HeaderY;
    unsigned MessageBoxY;
    unsigned MessageBoxH;
    unsigned MessageY;
    unsigned MessageBottom;
    int Index;
    const char *Title;
    unsigned Color;

    if (WindowId < 0) {
        return;
    }
    MainWidth = CHAT_WINDOW_WIDTH - MainX;
    if (MainWidth < 160u) {
        MainWidth = 160u;
    }
    PanelX = MainX + CHAT_PAD;
    PanelW = MainWidth - CHAT_PAD * 2u;
    if (PanelW < 120u) {
        PanelW = 120u;
    }

    /* 左绿 / 右底灰：底边与输入框底对齐 */
    (void)GraphicsFillRect(WindowId, 0, 0, SideWidth, CHAT_BODY_BOTTOM, COLOR_SIDE);
    (void)GraphicsFillRect(WindowId, SideWidth, 0, CHAT_SEPARATOR_WIDTH,
                            CHAT_BODY_BOTTOM, COLOR_SIDE_DEEP);
    (void)GraphicsFillRect(WindowId, MainX, 0, MainWidth, CHAT_BODY_BOTTOM,
                            COLOR_BODY);

    (void)GraphicsDrawString(WindowId, CHAT_PAD, CHAT_PAD + 2u, COLOR_SIDE_TEXT,
                             "会话");
    PaintSessionRow(WindowId, Connected);

    /* 右侧顶栏：加高白底，盖住标题+状态 */
    HeaderY = CHAT_PAD;
    (void)GraphicsFillRect(WindowId, PanelX, HeaderY, PanelW, CHAT_HEADER_HEIGHT,
                            COLOR_PANEL);
    (void)GraphicsDrawRect(WindowId, PanelX, HeaderY, PanelW, CHAT_HEADER_HEIGHT,
                           COLOR_BORDER);
    Title = Connected ? "Host（已连接）" : "Host（未连接）";
    (void)GraphicsDrawString(WindowId, PanelX + CHAT_PAD, HeaderY + 10u, COLOR_TITLE,
                             Title);
    if (StatusText[0] != 0) {
        (void)GraphicsDrawString(WindowId, PanelX + CHAT_PAD,
                                 HeaderY + 10u + CHAT_LINE_HEIGHT + 4u, COLOR_MUTED,
                                 StatusText);
    }

    /* 消息接收区单独画框 */
    MessageBoxY = HeaderY + CHAT_HEADER_HEIGHT + 8u;
    MessageBottom = CHAT_COMPOSE_TOP - 8u;
    if (MessageBottom <= MessageBoxY + 40u) {
        MessageBottom = MessageBoxY + 40u;
    }
    MessageBoxH = MessageBottom - MessageBoxY;
    (void)GraphicsFillRect(WindowId, PanelX, MessageBoxY, PanelW, MessageBoxH,
                            COLOR_PANEL);
    (void)GraphicsDrawRect(WindowId, PanelX, MessageBoxY, PanelW, MessageBoxH,
                           COLOR_BORDER);

    MessageY = MessageBoxY + CHAT_PAD;
    if (LogCount == 0) {
        (void)GraphicsDrawString(WindowId, PanelX + CHAT_PAD, MessageY, COLOR_MUTED,
                                 "点「连接」后输入并发送");
    } else {
        for (Index = 0; Index < LogCount; Index++) {
            if (MessageY + CHAT_LINE_HEIGHT > MessageBottom - 4u) {
                break;
            }
            if (strncmp(LogWho[Index], "我", 3) == 0) {
                Color = COLOR_ME;
            } else if (strncmp(LogWho[Index], "对方", 6) == 0) {
                Color = COLOR_PEER;
            } else {
                Color = COLOR_MUTED;
            }
            (void)GraphicsDrawString(WindowId, PanelX + CHAT_PAD, MessageY, Color,
                                     LogWho[Index]);
            (void)GraphicsDrawString(WindowId, PanelX + CHAT_PAD + CHAT_WHO_COLUMN,
                                     MessageY, COLOR_TEXT, LogLines[Index]);
            MessageY += CHAT_LINE_HEIGHT + 6u;
        }
    }

    UiRefresh(WindowId);
}
