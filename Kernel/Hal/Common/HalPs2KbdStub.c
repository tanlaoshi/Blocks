/*
 * HalPs2KbdStub.c — 非 X64：无 i8042
 */
#include "HalPs2Kbd.h"

int HalPs2KbdInit(void) {
    return 0;
}

int HalPs2KbdReady(void) {
    return 0;
}

int HalPs2KbdDiscardByte(void) {
    return 0;
}

int HalPs2KbdPollChar(char *Out) {
    (void)Out;
    return 0;
}
