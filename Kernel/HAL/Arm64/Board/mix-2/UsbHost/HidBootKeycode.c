/*
 * UsbHost/HidBootKeycode.c — HID Boot Usage → ASCII（无 IME）
 *
 * Boot 协议只见键盘页 6 键滚包：Caps/Num/Scroll + 107 常用键位可映。
 * 音量滚轮 / 宏键 / 鼠标键走 Consumer 或鼠接口 → 非本刀（须 Report 协议另开）。
 */
#include "UsbHost/Private.h"

#ifndef BOARD_MIX2_USB_HOST_BRINGUP
#define BOARD_MIX2_USB_HOST_BRINGUP 0
#endif

#if BOARD_MIX2_USB_HOST_BRINGUP

#define HID_MODIFIER_LEFT_SHIFT   (1u << 1)
#define HID_MODIFIER_RIGHT_SHIFT  (1u << 5)
#define HID_USAGE_TAB             0x2Bu
#define HID_USAGE_CAPS_LOCK       0x39u
#define HID_USAGE_SCROLL_LOCK     0x47u
#define HID_USAGE_DELETE          0x4Cu
#define HID_USAGE_KEYPAD_NUM_LOCK 0x53u
#define HID_USAGE_KEYPAD_SLASH    0x54u
#define HID_USAGE_KEYPAD_ASTERISK 0x55u
#define HID_USAGE_KEYPAD_MINUS    0x56u
#define HID_USAGE_KEYPAD_PLUS     0x57u
#define HID_USAGE_KEYPAD_ENTER    0x58u
#define HID_USAGE_KEYPAD_1        0x59u
#define HID_USAGE_KEYPAD_0        0x62u
#define HID_USAGE_KEYPAD_DOT      0x63u
#define HID_USAGE_NON_US_BACKSLASH 0x64u

static UINT8 sCapsLockOn;
static UINT8 sScrollLockOn;
/* 开机默认开；灯在锁键翻转时再 SET_REPORT（见 HidBootKeyboardLeds） */
static UINT8 sNumLockOn = 1u;

UINT8 UsbHostHidBootKeyboardLedOutputByte(void) {
    UINT8 LedByte = 0;

    if (sNumLockOn != 0) {
        LedByte |= (1u << 0);
    }
    if (sCapsLockOn != 0) {
        LedByte |= (1u << 1);
    }
    if (sScrollLockOn != 0) {
        LedByte |= (1u << 2);
    }
    return LedByte;
}

char UsbHostHidBootUsageToAscii(UINT8 Usage, UINT8 Modifier) {
    int Shift = ((Modifier & (HID_MODIFIER_LEFT_SHIFT | HID_MODIFIER_RIGHT_SHIFT)) != 0);
    int LetterShift;

    if (Usage == HID_USAGE_CAPS_LOCK) {
        sCapsLockOn ^= 1u;
        UsbHostHidBootKeyboardLedsPush();
        return 0;
    }
    if (Usage == HID_USAGE_KEYPAD_NUM_LOCK) {
        sNumLockOn ^= 1u;
        UsbHostHidBootKeyboardLedsPush();
        return 0;
    }
    if (Usage == HID_USAGE_SCROLL_LOCK) {
        sScrollLockOn ^= 1u;
        UsbHostHidBootKeyboardLedsPush();
        return 0;
    }

    if (Usage >= 0x04u && Usage <= 0x1Du) {
        char Letter = (char)('a' + (Usage - 0x04u));
        LetterShift = Shift ^ (sCapsLockOn != 0);
        if (LetterShift != 0) {
            Letter = (char)(Letter - 'a' + 'A');
        }
        return Letter;
    }
    if (Usage >= 0x1Eu && Usage <= 0x26u) {
        return Shift ? ")!@#$%^&*("[Usage - 0x1Eu] : (char)('1' + (Usage - 0x1Eu));
    }
    if (Usage == 0x27u) {
        return Shift ? ')' : '0';
    }
    if (Usage == 0x2Cu) {
        return ' ';
    }
    if (Usage == HID_USAGE_TAB) {
        return ' ';
    }
    if (Usage == 0x28u || Usage == HID_USAGE_KEYPAD_ENTER) {
        return '\n';
    }
    if (Usage == 0x2Au || Usage == HID_USAGE_DELETE) {
        return '\b';
    }
    if (Usage == 0x2Du) {
        return Shift ? '_' : '-';
    }
    if (Usage == 0x2Eu) {
        return Shift ? '+' : '=';
    }
    if (Usage == 0x2Fu) {
        return Shift ? '{' : '[';
    }
    if (Usage == 0x30u) {
        return Shift ? '}' : ']';
    }
    if (Usage == 0x31u || Usage == HID_USAGE_NON_US_BACKSLASH) {
        return Shift ? '|' : '\\';
    }
    if (Usage == 0x33u) {
        return Shift ? ':' : ';';
    }
    if (Usage == 0x34u) {
        return Shift ? '"' : '\'';
    }
    if (Usage == 0x35u) {
        return Shift ? '~' : '`';
    }
    if (Usage == 0x36u) {
        return Shift ? '<' : ',';
    }
    if (Usage == 0x37u) {
        return Shift ? '>' : '.';
    }
    if (Usage == 0x38u) {
        return Shift ? '?' : '/';
    }

    /* 小键盘运算符始终有效；数字随 NumLock */
    if (Usage == HID_USAGE_KEYPAD_SLASH) {
        return '/';
    }
    if (Usage == HID_USAGE_KEYPAD_ASTERISK) {
        return '*';
    }
    if (Usage == HID_USAGE_KEYPAD_MINUS) {
        return '-';
    }
    if (Usage == HID_USAGE_KEYPAD_PLUS) {
        return '+';
    }
    if (sNumLockOn != 0) {
        if (Usage >= HID_USAGE_KEYPAD_1 && Usage <= 0x61u) {
            return (char)('1' + (Usage - HID_USAGE_KEYPAD_1));
        }
        if (Usage == HID_USAGE_KEYPAD_0) {
            return '0';
        }
        if (Usage == HID_USAGE_KEYPAD_DOT) {
            return '.';
        }
    }
    return 0;
}

#else

UINT8 UsbHostHidBootKeyboardLedOutputByte(void) {
    return 0;
}

char UsbHostHidBootUsageToAscii(UINT8 Usage, UINT8 Modifier) {
    (void)Usage;
    (void)Modifier;
    return 0;
}

#endif
