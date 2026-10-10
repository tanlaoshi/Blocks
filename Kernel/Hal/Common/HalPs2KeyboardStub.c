/*
 * HalPs2KeyboardStub.c — 非 X64：无 i8042
 */
#include "HalPs2Keyboard.h"

int HalPs2KeyboardInitialize(void) {
    return 0;
}

int HalPs2KeyboardReady(void) {
    return 0;
}

int HalPs2KeyboardDiscardByte(void) {
    return 0;
}

int HalPs2KeyboardPollChar(char *Out) {
    (void)Out;
    return 0;
}
