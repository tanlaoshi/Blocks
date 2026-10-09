/*
 * HalPs2MouseStub.c — 非 X64：无 PS/2 鼠
 */
#include "HalPs2Mouse.h"
#include "HalPs2.h"

void HalPs2Poll(void) {
}

void HalPs2KbdFeed(UINT8 Byte) {
    (void)Byte;
}

int HalPs2MouseInit(void) {
    return -1;
}

int HalPs2MouseReady(void) {
    return 0;
}

void HalPs2MouseDropInput(void) {
}

int HalPs2MousePoll(HAL_MOUSE_PACKET *Out) {
    (void)Out;
    return 0;
}
