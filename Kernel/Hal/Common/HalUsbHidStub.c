/*
 * HalUsbHidStub.c — 非 X64：HID 门面空实现
 */
#include "HalUsbHid.h"

int HalUsbHidInitialize(void) {
    return 0;
}

int HalUsbHidKeyboardReady(void) {
    return 0;
}

int HalUsbHidMouseReady(void) {
    return 0;
}

void HalUsbHidService(void) {
}

int HalUsbHidPollChar(char *Out) {
    (void)Out;
    return 0;
}

int HalUsbHidPollMouse(HAL_MOUSE_PACKET *Out) {
    (void)Out;
    return 0;
}
