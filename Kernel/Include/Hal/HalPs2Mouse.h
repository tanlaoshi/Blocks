/*
 * HalPs2Mouse.h — PS/2 鼠标门面（K17 最小）
 *
 * 【初学者】
 * 与键盘共用 i8042；状态口 bit5=1 表示数据来自鼠标。
 * 相对移动 + 左键；USB HID 鼠另刀。
 */
#ifndef HAL_PS2_MOUSE_H
#define HAL_PS2_MOUSE_H

#include "BootTypes.h"

typedef struct {
    INT32 Dx;
    INT32 Dy;
    UINT8 Buttons; /* bit0=左 bit1=右 bit2=中 */
} HAL_MOUSE_PACKET;

int HalPs2MouseInit(void);
int HalPs2MouseReady(void);
/* 凑齐一包返回 1；无数据 0 */
int HalPs2MousePoll(HAL_MOUSE_PACKET *Out);

#endif
