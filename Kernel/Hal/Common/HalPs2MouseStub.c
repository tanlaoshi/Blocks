/*
 * HalPs2MouseStub.c — 非 X64：无 PS/2 鼠
 */
#include "HalPs2Mouse.h"

int HalPs2MouseInit(void) {
    return -1;
}

int HalPs2MouseReady(void) {
    return 0;
}

int HalPs2MousePoll(HAL_MOUSE_PACKET *Out) {
    (void)Out;
    return 0;
}
