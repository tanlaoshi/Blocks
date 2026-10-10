/*
 * HalUsbHidStub.c — 非 X64：无 USB HID
 *
 * 【初学者】
 * - Hal 占位；UsbInitialize 跳过 xHCI 键鼠。
 * - 真实现：Hal/X64/Drivers/UsbHid/。
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
