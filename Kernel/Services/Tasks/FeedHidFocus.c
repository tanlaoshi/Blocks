/*
 * FeedHidFocus.c — FeedHid 按焦点窗投递（自 Hid.c 纯搬 · PR-NORM-7）
 *
 * 键位→ASCII / Caps·Num 锁灯 / Esc·方向·编辑快捷键；返回 1=已消费。
 */
#include "TasksPrivate.h"
#include "Hal.h"
#include "HIDKeyboard.h"
#include "Console.h"
#include "Gui.h"
#include "SettingsUi.h"
#include "FilesUi.h"
#include "EditUi.h"
#include "TtyUi.h"
#include "StoreUi.h"
#include "ShellCommands.h"

int FeedHidTrySettings(UINT8 Key, const HAL_KEYBOARD_REPORT *Report) {
    if (!SettingsUiIsFocused()) {
        return 0;
    }
    if (Key == HID_KEY_ESCAPE) {
        SettingsUiOnEscape();
        return 1;
    }
    if (Key == HID_KEY_LEFT || Key == HID_KEY_RIGHT ||
        Key == HID_KEY_UP || Key == HID_KEY_DOWN) {
        GuiOnArrowKey(Key);
        return 1;
    }
    {
        char Character = HIDKeyCodeToASCII(Key, Report->ModifierKeys);
        if (Character >= '0' && Character <= '9') {
            SettingsUiOnDigit(Character);
        }
    }
    return 1;
}

int FeedHidTryEdit(UINT8 Key, const HAL_KEYBOARD_REPORT *Report) {
    if (!EditUiIsFocused()) {
        return 0;
    }
    if (Key == HID_KEY_ESCAPE) {
        EditUiOnEscape();
        return 1;
    }
    if (Key == HID_KEY_ENTER) {
        EditUiOnEnter();
        return 1;
    }
    if (Key == HID_KEY_UP || Key == HID_KEY_DOWN) {
        EditUiOnArrow(Key == HID_KEY_DOWN);
        HidRepeatArm(Key);
        return 1;
    }
    if (Key == HID_KEY_LEFT || Key == HID_KEY_RIGHT) {
        EditUiOnArrowLeftRight(Key == HID_KEY_RIGHT);
        HidRepeatArm(Key);
        return 1;
    }
    if (Key == HID_KEY_BACKSPACE) {
        EditUiOnBackspace();
        return 1;
    }
    if (Key == HID_KEY_DELETE) {
        EditUiOnDeleteKey();
        return 1;
    }
    if (Key == HID_KEY_S &&
        (Report->ModifierKeys & (HID_MOD_LCTRL | HID_MOD_RCTRL))) {
        EditUiSave();
        return 1;
    }
    if (HIDKeyboardApplyLockKey(Key)) {
        HalKeyboardSetLeds(HIDKeyboardGetLeds());
        return 1;
    }
    {
        char Character = HIDKeyCodeToASCII(Key, Report->ModifierKeys);
        if (Character != 0) {
            EditUiOnChar(Character);
        }
    }
    return 1;
}

int FeedHidTryTty(UINT8 Key, const HAL_KEYBOARD_REPORT *Report) {
    if (!TtyUiIsFocused()) {
        return 0;
    }
    if (Key == HID_KEY_ESCAPE) {
        TtyUiOnEscape();
        return 1;
    }
    if (Key == HID_KEY_ENTER) {
        TtyUiOnEnter();
        return 1;
    }
    if (Key == HID_KEY_BACKSPACE) {
        TtyUiOnBackspace();
        return 1;
    }
    if (HIDKeyboardApplyLockKey(Key)) {
        HalKeyboardSetLeds(HIDKeyboardGetLeds());
        return 1;
    }
    {
        char Character = HIDKeyCodeToASCII(Key, Report->ModifierKeys);
        if (Character != 0) {
            TtyUiOnChar(Character);
        }
    }
    return 1;
}

int FeedHidTryStore(UINT8 Key) {
    if (!StoreUiIsFocused()) {
        return 0;
    }
    if (Key == HID_KEY_ESCAPE) {
        StoreUiOnEscape();
    }
    return 1;
}

int FeedHidTryFiles(UINT8 Key, const HAL_KEYBOARD_REPORT *Report) {
    if (!FilesUiIsFocused()) {
        return 0;
    }
    if (Key == HID_KEY_ESCAPE) {
        FilesUiOnEscape();
        return 1;
    }
    if (Key == HID_KEY_ENTER) {
        FilesUiOnEnter();
        return 1;
    }
    if (Key == HID_KEY_UP || Key == HID_KEY_DOWN) {
        FilesUiOnArrow(Key == HID_KEY_DOWN);
        return 1;
    }
    if (Key == HID_KEY_BACKSPACE) {
        FilesUiOnBackspace();
        return 1;
    }
    if (Key == HID_KEY_DELETE) {
        FilesUiOnDeleteKey();
        return 1;
    }
    if (HIDKeyboardApplyLockKey(Key)) {
        HalKeyboardSetLeds(HIDKeyboardGetLeds());
        return 1;
    }
    {
        char Character = HIDKeyCodeToASCII(Key, Report->ModifierKeys);
        if (Character != 0) {
            FilesUiOnChar(Character);
        }
    }
    return 1;
}

int FeedHidTryUserWindow(UINT8 Key) {
    if (GuiFocusKind() != GUI_WIN_USER) {
        return 0;
    }
    if (HIDKeyboardApplyLockKey(Key)) {
        HalKeyboardSetLeds(HIDKeyboardGetLeds());
        return 1;
    }
    GuiUserEnqueueKey(Key);
    return 1;
}

int FeedHidTryStdinHold(UINT8 Key, const HAL_KEYBOARD_REPORT *Report) {
    /*
     * exec 占 stdin：默认可打印进 stdin 环（控制台 Guest）。
     * Guest 已有 GUI 窗且焦点在 Shell：键还给壳（切回 Shell 可打字；
     * Chat 焦点仍走上方 GUI_WIN_USER）。
     */
    if (!(gConsole.StdinUserHold() &&
          !(GuiShellAcceptsInput() && GuiUserWindowAlive()))) {
        return 0;
    }
    if (Key == HID_KEY_ENTER) {
        gConsole.StdinPut('\n');
        return 1;
    }
    if (Key == HID_KEY_BACKSPACE) {
        gConsole.StdinPut('\b');
        return 1;
    }
    if (HIDKeyboardApplyLockKey(Key)) {
        HalKeyboardSetLeds(HIDKeyboardGetLeds());
        return 1;
    }
    if (Key == HID_KEY_C &&
        (Report->ModifierKeys & (HID_MOD_LCTRL | HID_MOD_RCTRL))) {
        ShellOnInterrupt();
        return 1;
    }
    {
        char Character = HIDKeyCodeToASCII(Key, Report->ModifierKeys);
        if (Character != 0) {
            gConsole.StdinPut(Character);
        }
    }
    return 1;
}

void FeedHidShellDefault(UINT8 Key, const HAL_KEYBOARD_REPORT *Report) {
    if (Key == HID_KEY_ENTER) {
        /* gConsole.OnEnter → EnsureShell：空桌面时开 Shell */
        gConsole.OnEnter();
        return;
    }
    if (Key == HID_KEY_UP || Key == HID_KEY_DOWN) {
        /* Shell 焦点：↑↓ 历史；其它窗仍挪光标 */
        if (GuiShellAcceptsInput()) {
            gConsole.OnHistHid(Key, 0);
        } else {
            GuiOnArrowKey(Key);
        }
        return;
    }
    if (Key == HID_KEY_LEFT || Key == HID_KEY_RIGHT) {
        GuiOnArrowKey(Key);
        return;
    }
    if (Key == HID_KEY_BACKSPACE) {
        if (GuiShellAcceptsInput()) {
            gConsole.OnBackspace();
        }
        return;
    }
    if (HIDKeyboardApplyLockKey(Key)) {
        HalKeyboardSetLeds(HIDKeyboardGetLeds());
        return;
    }
    if (Key == HID_KEY_C &&
        (Report->ModifierKeys & (HID_MOD_LCTRL | HID_MOD_RCTRL))) {
        ShellOnInterrupt();
        return;
    }
    {
        char Character = HIDKeyCodeToASCII(Key, Report->ModifierKeys);
        if (Character != 0) {
            gConsole.OnChar(Character);
        }
    }
}
