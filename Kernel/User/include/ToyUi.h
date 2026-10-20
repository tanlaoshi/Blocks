/*
 * ToyUi.h — 用户态控件薄库（libToyUi）
 *
 * ABI：改签名 / 删已有事件语义须递增 UI_ABI_VERSION_MAJOR。
 * 1.0.0：窗 / 标签 / 底栏按钮 0..3 / Poll。
 * 1.1.0：+ 复选框 / 列表 / 输入框（用户态绘制；客户区点击经 Poll 分发）。
 * 1.2.0：+ 键盘入窗（焦点 USER 时 Poll 返回 KEY=300+HID）。
 * 依赖 libGraphics + G14/G15 窗口 syscall；不链接内核 UI.c。
 */
#ifndef UI_H
#define UI_H

#include <Graphics.h>

#define UI_ABI_VERSION_MAJOR 2
#define UI_ABI_VERSION_MINOR 0
#define UI_ABI_VERSION_PATCH 0
#define UI_ABI_VERSION_STRING "2.0.0"

#define UI_EVENT_NONE          0
#define UI_EVENT_CLOSE         1
#define UI_EVENT_CLICK         2  /* 客户区点击未命中控件 */
#define UI_EVENT_BUTTON_BASE 100
#define UI_BUTTON_EVENT(Id) (UI_EVENT_BUTTON_BASE + (Id))
#define UI_EVENT_CHECK_BASE 200
#define UI_CHECK_EVENT(Id)  (UI_EVENT_CHECK_BASE + (Id))
#define UI_EVENT_LIST_BASE  220
#define UI_LIST_EVENT(Id)   (UI_EVENT_LIST_BASE + (Id))
#define UI_EVENT_TEXT_BASE  240
#define UI_TEXT_EVENT(Id)   (UI_EVENT_TEXT_BASE + (Id))
/* 与内核点击包基址一致；KEY 须 < 此值 */
#define UI_CLICK_PACK_BASE  400
/* 键盘：300 + HID usage（字母 0x04..、方向 0x4F..）；<400 避开点击包 */
#define UI_EVENT_KEY_BASE   300
#define UI_KEY_EVENT(Hid)   (UI_EVENT_KEY_BASE + (Hid))
#define UI_KEY_CODE(Ev)     ((Ev) - UI_EVENT_KEY_BASE)
#define UI_IS_KEY(Ev) \
    ((Ev) >= UI_EVENT_KEY_BASE && (Ev) < UI_CLICK_PACK_BASE)

#define UI_BUTTON_ID_MIN 0
#define UI_BUTTON_ID_MAX 3
#define UI_CHECK_ID_MAX  1
#define UI_LIST_ID_MAX   0
#define UI_TEXT_ID_MAX   0
#define UI_LIST_ITEM_MAX 4
#define UI_TEXT_MAX      24

/* 成功返回 wid（>=0）；失败 -1 */
int UiCreateWindow(const char *Title, unsigned Width, unsigned Height);
/* 客户区标签（文字 damage）；成功 0，失败 -1 */
int UiSetLabel(int WindowId, const char *Text);
/* ButtonId 0..3；底栏自动排布；成功 0，失败 -1 */
int UiAddButton(int WindowId, int ButtonId, const char *Label);
/*
 * UiPoll — 非阻塞取事件
 * 返回：NONE / CLOSE / CLICK / BUTTON / CHECK / LIST / TEXT / KEY；错误 -1
 * 有焦点的输入框时，可打印键会写入缓冲（仍返回 KEY 事件）。
 */
int UiPoll(int WindowId);

/* PR-A-ui-api：客户区控件（坐标与 Graphics 相同，含 pad） */
int UiAddCheckBox(int WindowId, int CheckId, unsigned X, unsigned Y);
int UiSetCheckBox(int WindowId, int CheckId, int Checked);
int UiGetCheckBox(int WindowId, int CheckId);

int UiAddList(int WindowId, int ListId, unsigned X, unsigned Y, unsigned W);
int UiListAddItem(int WindowId, int ListId, const char *Text);
int UiListSetSelected(int WindowId, int ListId, int Index);
int UiListGetSelected(int WindowId, int ListId);

int UiAddTextField(int WindowId, int FieldId, unsigned X, unsigned Y,
                      unsigned W);
int UiSetTextField(int WindowId, int FieldId, const char *Text);
int UiGetTextField(int WindowId, int FieldId, char *Buf, unsigned Cap);

#endif
