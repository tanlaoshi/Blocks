/*
 * UiWidgets.c — PR-S-toyui-1：复选框 / 列表 / 输入框
 *
 * 从 Ui.c 原样搬家；不改语义。
 */
#include <Ui.h>
#include <Graphics.h>
#include <UiStyle.h>

#include "Private.h"

void UiRedrawWin(int WindowId, UI_WIN *St) {
    int I;
    unsigned RowY;
    const UI_STYLE *S = UiStyleCurrent();

    if (!St) {
        return;
    }
    for (I = 0; I <= UI_CHECK_ID_MAX; I++) {
        if (!St->Check[I].Used) {
            continue;
        }
        GraphicsDrawRect(WindowId, St->Check[I].X, St->Check[I].Y,
                       UI_CHECK_SIZE, UI_CHECK_SIZE, S->CheckBorder);
        if (St->Check[I].Checked) {
            GraphicsFillRect(WindowId, St->Check[I].X + 3, St->Check[I].Y + 3,
                           UI_CHECK_SIZE - 6, UI_CHECK_SIZE - 6,
                           S->CheckFillOn);
        } else {
            GraphicsFillRect(WindowId, St->Check[I].X + 3, St->Check[I].Y + 3,
                           UI_CHECK_SIZE - 6, UI_CHECK_SIZE - 6,
                           S->CheckFillOff);
        }
    }
    if (St->List.Used) {
        for (I = 0; I < St->List.Count; I++) {
            RowY = St->List.Y + (unsigned)I * UI_LIST_ROW;
            if (I == St->List.Selected) {
                GraphicsFillRect(WindowId, St->List.X, RowY, St->List.W,
                               UI_LIST_ROW - 2, S->ListRowSelBg);
            } else {
                GraphicsFillRect(WindowId, St->List.X, RowY, St->List.W,
                               UI_LIST_ROW - 2, S->ListRowBg);
            }
            GraphicsDrawRect(WindowId, St->List.X, RowY, St->List.W,
                           UI_LIST_ROW - 2, S->ListRowBorder);
        }
    }
    if (St->Field.Used) {
        GraphicsFillRect(WindowId, St->Field.X, St->Field.Y, St->Field.W,
                       UI_FIELD_H, S->FieldBg);
        GraphicsDrawRect(WindowId, St->Field.X, St->Field.Y, St->Field.W,
                       UI_FIELD_H,
                       St->Field.Focus ? S->FieldBorderFocus : S->FieldBorder);
    }
}

int UiHitWidgets(int WindowId, unsigned X, unsigned Y) {
    UI_WIN *St;
    int I;
    unsigned RowY;

    St = UiWinState(WindowId);
    if (!St) {
        return UI_EVENT_CLICK;
    }
    for (I = 0; I <= UI_CHECK_ID_MAX; I++) {
        if (St->Check[I].Used &&
            UiInRect(X, Y, St->Check[I].X, St->Check[I].Y, UI_CHECK_SIZE,
                        UI_CHECK_SIZE)) {
            St->Check[I].Checked = !St->Check[I].Checked;
            UiRedrawWin(WindowId, St);
            return UI_CHECK_EVENT(I);
        }
    }
    if (St->List.Used) {
        for (I = 0; I < St->List.Count; I++) {
            RowY = St->List.Y + (unsigned)I * UI_LIST_ROW;
            if (UiInRect(X, Y, St->List.X, RowY, St->List.W,
                            UI_LIST_ROW - 2)) {
                St->List.Selected = I;
                UiCopyCap(St->Field.Text, sizeof(St->Field.Text),
                             St->List.Items[I]);
                UiRedrawWin(WindowId, St);
                return UI_LIST_EVENT(0);
            }
        }
    }
    if (St->Field.Used &&
        UiInRect(X, Y, St->Field.X, St->Field.Y, St->Field.W, UI_FIELD_H)) {
        St->Field.Focus = 1;
        UiRedrawWin(WindowId, St);
        return UI_TEXT_EVENT(0);
    }
    if (St->Field.Used) {
        St->Field.Focus = 0;
    }
    /*
     * 客户区空白点击会走 GuiRaiseToFront→PaintUserClient，整块刷底，
     * 用户态复选/列表像素被清掉。未命中控件时仍须重画。
     */
    UiRedrawWin(WindowId, St);
    return UI_EVENT_CLICK;
}

int UiAddCheckBox(int WindowId, int CheckId, unsigned X, unsigned Y) {
    UI_WIN *St;

    St = UiWinState(WindowId);
    if (!St || CheckId < 0 || CheckId > UI_CHECK_ID_MAX) {
        return -1;
    }
    St->Check[CheckId].Used = 1;
    St->Check[CheckId].X = X;
    St->Check[CheckId].Y = Y;
    St->Check[CheckId].Checked = 0;
    UiRedrawWin(WindowId, St);
    return 0;
}

int UiSetCheckBox(int WindowId, int CheckId, int Checked) {
    UI_WIN *St;

    St = UiWinState(WindowId);
    if (!St || CheckId < 0 || CheckId > UI_CHECK_ID_MAX ||
        !St->Check[CheckId].Used) {
        return -1;
    }
    St->Check[CheckId].Checked = Checked ? 1 : 0;
    UiRedrawWin(WindowId, St);
    return 0;
}

int UiGetCheckBox(int WindowId, int CheckId) {
    UI_WIN *St;

    St = UiWinState(WindowId);
    if (!St || CheckId < 0 || CheckId > UI_CHECK_ID_MAX ||
        !St->Check[CheckId].Used) {
        return -1;
    }
    return St->Check[CheckId].Checked;
}

int UiAddList(int WindowId, int ListId, unsigned X, unsigned Y, unsigned W) {
    UI_WIN *St;

    St = UiWinState(WindowId);
    if (!St || ListId != 0 || W < 8) {
        return -1;
    }
    St->List.Used = 1;
    St->List.X = X;
    St->List.Y = Y;
    St->List.W = W;
    St->List.Count = 0;
    St->List.Selected = -1;
    UiRedrawWin(WindowId, St);
    return 0;
}

int UiListAddItem(int WindowId, int ListId, const char *Text) {
    UI_WIN *St;

    St = UiWinState(WindowId);
    if (!St || ListId != 0 || !St->List.Used || !Text) {
        return -1;
    }
    if (St->List.Count >= UI_LIST_ITEM_MAX) {
        return -1;
    }
    UiCopyCap(St->List.Items[St->List.Count], sizeof(St->List.Items[0]), Text);
    St->List.Count++;
    UiRedrawWin(WindowId, St);
    return 0;
}

int UiListSetSelected(int WindowId, int ListId, int Index) {
    UI_WIN *St;

    St = UiWinState(WindowId);
    if (!St || ListId != 0 || !St->List.Used) {
        return -1;
    }
    if (Index < 0 || Index >= St->List.Count) {
        return -1;
    }
    St->List.Selected = Index;
    UiRedrawWin(WindowId, St);
    return 0;
}

int UiListGetSelected(int WindowId, int ListId) {
    UI_WIN *St;

    St = UiWinState(WindowId);
    if (!St || ListId != 0 || !St->List.Used) {
        return -1;
    }
    return St->List.Selected;
}

int UiAddTextField(int WindowId, int FieldId, unsigned X, unsigned Y,
                      unsigned W) {
    UI_WIN *St;

    St = UiWinState(WindowId);
    if (!St || FieldId != 0 || W < 8) {
        return -1;
    }
    St->Field.Used = 1;
    St->Field.X = X;
    St->Field.Y = Y;
    St->Field.W = W;
    St->Field.Focus = 0;
    St->Field.Text[0] = 0;
    UiRedrawWin(WindowId, St);
    return 0;
}

int UiSetTextField(int WindowId, int FieldId, const char *Text) {
    UI_WIN *St;

    St = UiWinState(WindowId);
    if (!St || FieldId != 0 || !St->Field.Used) {
        return -1;
    }
    UiCopyCap(St->Field.Text, sizeof(St->Field.Text), Text);
    UiRedrawWin(WindowId, St);
    return 0;
}

int UiGetTextField(int WindowId, int FieldId, char *Buf, unsigned Cap) {
    UI_WIN *St;

    St = UiWinState(WindowId);
    if (!St || FieldId != 0 || !St->Field.Used || !Buf || Cap == 0) {
        return -1;
    }
    UiCopyCap(Buf, Cap, St->Field.Text);
    return 0;
}

/* 无 Shift：字母小写、数字与空白；供焦点输入框 */
static char UiHidAscii(int Hid) {
    if (Hid >= UI_HID_A && Hid <= 0x1D) {
        return (char)('a' + (Hid - UI_HID_A));
    }
    if (Hid >= 0x1E && Hid <= 0x26) {
        return (char)('1' + (Hid - 0x1E));
    }
    if (Hid == 0x27) {
        return '0';
    }
    if (Hid == UI_HID_SPACE) {
        return ' ';
    }
    return 0;
}

int UiApplyKeyToField(int WindowId, int Hid) {
    UI_WIN *St;
    char C;
    unsigned Len;

    St = UiWinState(WindowId);
    if (!St || !St->Field.Used || !St->Field.Focus) {
        return 0;
    }
    if (Hid == UI_HID_BACKSPACE) {
        Len = 0;
        while (St->Field.Text[Len]) {
            Len++;
        }
        if (Len > 0) {
            St->Field.Text[Len - 1] = 0;
            UiRedrawWin(WindowId, St);
        }
        return 1;
    }
    C = UiHidAscii(Hid);
    if (!C) {
        return 0;
    }
    Len = 0;
    while (St->Field.Text[Len]) {
        Len++;
    }
    if (Len + 1 >= UI_TEXT_MAX) {
        return 1;
    }
    St->Field.Text[Len] = C;
    St->Field.Text[Len + 1] = 0;
    UiRedrawWin(WindowId, St);
    return 1;
}
