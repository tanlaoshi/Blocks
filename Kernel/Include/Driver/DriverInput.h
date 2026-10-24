/*
 * DriverInput.h — Input 类适配（PR-D3；PR-H-input-mux）
 *
 * 驱动 Bind 时调用 DriverInputAttach（可多次，多源聚合）；
 * Common 仍只见 HalInput*。
 */
#ifndef DRIVER_INPUT_H
#define DRIVER_INPUT_H

#include "HalDevices.h"

typedef struct {
    void (*Poll)(void);
    /* 可选；NULL = 无键（鼠-only backend，如 Pi DWC2 HID） */
    int (*KeyboardDequeue)(HAL_KEYBOARD_REPORT *Report);
    /* 可选；NULL → HalKeyboardSetLeds 返回 -1 */
    int (*KeyboardSetLeds)(UINT8 Leds);
    int (*MousePresent)(void);
    int (*MouseDequeue)(HAL_MOUSE_REPORT *Report);
} INPUT_BACKEND;

int DriverInputAttach(const INPUT_BACKEND *Backend);
int DriverInputReady(void);

void DriverInputPoll(void);
int DriverInputKeyboardDequeue(HAL_KEYBOARD_REPORT *Report);
int DriverInputKeyboardSetLeds(UINT8 Leds);
int DriverInputMousePresent(void);
int DriverInputMouseDequeue(HAL_MOUSE_REPORT *Report);

#endif
