/*
 * HalPs2MouseStub.c — 非 X64：无 PS/2 鼠标
 *
 * 【初学者】
 * - Hal 占位；Gui 指针设备可回退 USB HID 或键盘-only。
 * - 真实现：Hal/X64/HalPs2Mouse.c。
 */
#include "HalPs2Mouse.h"
#include "HalPs2.h"

void HalPs2Poll(void) {
}

void HalPs2KeyboardFeed(UINT8 Byte) {
    (void)Byte;
}

int HalPs2MouseInitialize(void) {
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
