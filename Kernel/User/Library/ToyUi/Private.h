/*
 * UiPrivate.h — PR-S-toyui-1：libToyUi 模块内共享
 */
#ifndef UI_PRIVATE_H
#define UI_PRIVATE_H

#include <ToyUi.h>

#define UI_WIN_MAX 6
#define UI_CLICK_PACK_BASE 400
#define UI_CLICK_SHIFT 10
#define UI_CLICK_MASK 1023
#define UI_CHECK_SIZE 16u
#define UI_LIST_ROW 18u
#define UI_FIELD_H 22u

/* USB HID keyboard usage（与内核 HIDKeyboard.h 对齐的子集） */
#define UI_HID_A          0x04
#define UI_HID_ENTER      0x28
#define UI_HID_ESCAPE     0x29
#define UI_HID_BACKSPACE  0x2A
#define UI_HID_SPACE      0x2C
#define UI_HID_RIGHT      0x4F
#define UI_HID_LEFT       0x50
#define UI_HID_DOWN       0x51
#define UI_HID_UP         0x52

typedef struct {
    int Used;
    unsigned X;
    unsigned Y;
    int Checked;
} UI_CHECK;

typedef struct {
    int Used;
    unsigned X;
    unsigned Y;
    unsigned W;
    int Count;
    int Selected;
    char Items[UI_LIST_ITEM_MAX][16];
} UI_LIST;

typedef struct {
    int Used;
    unsigned X;
    unsigned Y;
    unsigned W;
    int Focus;
    char Text[UI_TEXT_MAX];
} UI_FIELD;

typedef struct {
    UI_CHECK Check[UI_CHECK_ID_MAX + 1];
    UI_LIST List;
    UI_FIELD Field;
} UI_WIN;

UI_WIN *UiWinState(int WindowId);
int UiInRect(unsigned X, unsigned Y, unsigned Rx, unsigned Ry, unsigned Rw,
                unsigned Rh);
void UiCopyCap(char *Dst, unsigned Cap, const char *Src);
void UiRedrawWin(int WindowId, UI_WIN *St);
int UiHitWidgets(int WindowId, unsigned X, unsigned Y);
/* 有焦点输入框时消费可打印/退格；返回是否已处理缓冲 */
int UiApplyKeyToField(int WindowId, int Hid);

#endif
