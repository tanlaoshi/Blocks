/*
 * Ui.c — libUi 核心：窗 / 标签 / 底栏按钮 / Poll（PR-S-toyui-1）
 */
#include <Ui.h>
#include <string.h>
#include <unistd.h>
#include <Blocks/syscall.h>

#include "Private.h"

static UI_WIN s_Wins[UI_WIN_MAX];

UI_WIN *UiWinState(int WindowId) {
    if (WindowId < 0 || WindowId >= UI_WIN_MAX) {
        return 0;
    }
    return &s_Wins[WindowId];
}

int UiInRect(unsigned X, unsigned Y, unsigned Rx, unsigned Ry, unsigned Rw,
                unsigned Rh) {
    return X >= Rx && Y >= Ry && X < Rx + Rw && Y < Ry + Rh;
}

void UiCopyCap(char *Dst, unsigned Cap, const char *Src) {
    unsigned I;

    if (!Dst || Cap == 0) {
        return;
    }
    if (!Src) {
        Dst[0] = 0;
        return;
    }
    for (I = 0; I + 1 < Cap && Src[I]; I++) {
        Dst[I] = Src[I];
    }
    Dst[I] = 0;
}

int UiCreateWindow(const char *Title, unsigned Width, unsigned Height) {
    int Wid;
    UI_WIN *St;

    Wid = create_window(Title, Width, Height);
    St = UiWinState(Wid);
    if (St) {
        memset(St, 0, sizeof(*St));
    }
    return Wid;
}

int UiSetLabel(int WindowId, const char *Text) {
    int Rc;

    Rc = GraphicsDamageText(WindowId, Text);
    if (Rc == 0) {
        UiRedrawWin(WindowId, UiWinState(WindowId));
    }
    return Rc;
}

int UiAddButton(int WindowId, int ButtonId, const char *Label) {
    long R;

    if (WindowId < 0 || ButtonId < UI_BUTTON_ID_MIN ||
        ButtonId > UI_BUTTON_ID_MAX || !Label) {
        return -1;
    }
    R = toy_ui_button(WindowId, ButtonId, Label);
    if (R < 0) {
        return -1;
    }
    UiRedrawWin(WindowId, UiWinState(WindowId));
    return 0;
}

int UiPoll(int WindowId) {
    long R;
    unsigned X;
    unsigned Y;
    int Hid;

    if (WindowId < 0) {
        return -1;
    }
    R = toy_poll_input(WindowId);
    if (R < 0) {
        return -1;
    }
    if (R >= UI_CLICK_PACK_BASE) {
        X = (unsigned)(R - UI_CLICK_PACK_BASE) & UI_CLICK_MASK;
        Y = (unsigned)(R - UI_CLICK_PACK_BASE) >> UI_CLICK_SHIFT;
        return UiHitWidgets(WindowId, X, Y);
    }
    if (R >= UI_EVENT_KEY_BASE && R < UI_CLICK_PACK_BASE) {
        Hid = (int)(R - UI_EVENT_KEY_BASE);
        UiApplyKeyToField(WindowId, Hid);
        return (int)R;
    }
    return (int)R;
}
