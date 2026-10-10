/*
 * HalPs2KeyboardStub.c — 非 X64：无 PS/2 键盘
 *
 * 【初学者】
 * - Hal 占位；Gui/Console 可探测 HalPs2KeyboardReady==0。
 * - 真实现：Hal/X64/HalPs2Keyboard.c。
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
