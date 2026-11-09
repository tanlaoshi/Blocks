/*
 * UsbHost/HidBootKeycode.c — HID Boot Usage → ASCII（无 IME）
 */
#include "UsbHost/Private.h"

#ifndef BOARD_PIXEL8_USB_HOST_BRINGUP
#define BOARD_PIXEL8_USB_HOST_BRINGUP 0
#endif

#if BOARD_PIXEL8_USB_HOST_BRINGUP

#define HID_MODIFIER_LEFT_SHIFT  (1u << 1)
#define HID_MODIFIER_RIGHT_SHIFT (1u << 5)

char UsbHostHidBootUsageToAscii(UINT8 Usage, UINT8 Modifier) {
    int Shift = ((Modifier & (HID_MODIFIER_LEFT_SHIFT | HID_MODIFIER_RIGHT_SHIFT)) != 0);

    if (Usage >= 0x04u && Usage <= 0x1Du) {
        char Letter = (char)('a' + (Usage - 0x04u));
        if (Shift != 0) {
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
    if (Usage == 0x28u || Usage == 0x58u) {
        return '\n';
    }
    if (Usage == 0x2Au) {
        return '\b';
    }
    if (Usage == 0x2Du) {
        return Shift ? '_' : '-';
    }
    if (Usage == 0x2Eu) {
        return Shift ? '+' : '=';
    }
    if (Usage == 0x33u) {
        return Shift ? ':' : ';';
    }
    if (Usage == 0x34u) {
        return Shift ? '"' : '\'';
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
    return 0;
}

#else

char UsbHostHidBootUsageToAscii(UINT8 Usage, UINT8 Modifier) {
    (void)Usage;
    (void)Modifier;
    return 0;
}

#endif
