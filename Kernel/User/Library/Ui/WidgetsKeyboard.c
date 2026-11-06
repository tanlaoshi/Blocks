/*
 * WidgetsKeyboard.c — 焦点输入框键入（从 Widgets.c 抽出）
 */
#include <Ui/Ui.h>

#include "Private.h"

static char UiHidAscii(int Usage) {
    if (Usage >= UI_HID_A && Usage <= 0x1D) {
        return (char)('a' + (Usage - UI_HID_A));
    }
    if (Usage >= 0x1E && Usage <= 0x26) {
        return (char)('1' + (Usage - 0x1E));
    }
    if (Usage == 0x27) {
        return '0';
    }
    if (Usage == UI_HID_SPACE) {
        return ' ';
    }
    return 0;
}

int UiApplyKeyToField(int WindowId, int Usage) {
    UI_WIN *State;
    char Character;
    unsigned Length;

    State = UiWinState(WindowId);
    if (!State || !State->Field.Used || !State->Field.Focus) {
        return 0;
    }
    if (Usage == UI_HID_BACKSPACE) {
        Length = 0;
        while (State->Field.Text[Length]) {
            Length++;
        }
        if (Length > 0) {
            State->Field.Text[Length - 1] = 0;
            UiRedrawWin(WindowId, State);
        }
        return 1;
    }
    Character = UiHidAscii(Usage);
    if (!Character) {
        return 0;
    }
    Length = 0;
    while (State->Field.Text[Length]) {
        Length++;
    }
    if (Length + 1 >= UI_TEXT_MAX) {
        return 1;
    }
    State->Field.Text[Length] = Character;
    State->Field.Text[Length + 1] = 0;
    UiRedrawWin(WindowId, State);
    return 1;
}
