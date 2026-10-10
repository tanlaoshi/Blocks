/*
 * HalPs2Keyboard.h — PS/2 键盘门面（K15 最小）
 *
 * 【初学者】
 * QEMU GTK 窗按键通常进 i8042，不是 COM1。
 * X64 真读 0x60/0x64；其它 Arch stub。USB HID 枚举另刀。
 */
#ifndef HAL_PS2_KBD_H
#define HAL_PS2_KBD_H

#include "BootTypes.h"

/* 排空缓冲；成功 0（无控制器也 0，Ready=0） */
int HalPs2KeyboardInitialize(void);
int HalPs2KeyboardReady(void);
/* 消化 1 字节键盘数据（含断码/修饰键）；1=读了，0=无。供鼠轮询交错排空 OBF */
int HalPs2KeyboardDiscardByte(void);
/*
 * 若有可打印/控制 ASCII，写入 *Out 并返回 1；
 * 无数据或忽略的扫描码返回 0。
 */
int HalPs2KeyboardPollChar(char *Out);

#endif
