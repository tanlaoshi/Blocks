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
    UINT8 Buttons;  /* bit0=左 bit1=右 bit2=中 */
    UINT8 Absolute; /* 1：Dx/Dy 为 0..32767 平板坐标（USB tablet） */
} HAL_MOUSE_PACKET;

int HalPs2MouseInitialize(void);
int HalPs2MouseReady(void);
/* 兼容旧名：现为 HalPs2Poll（排空 OBF→队列） */
void HalPs2MouseDropInput(void);
/* 从软件队列取一包；先 HalPs2Poll 再调。1=有包，0=空 */
int HalPs2MousePoll(HAL_MOUSE_PACKET *Out);

#endif
