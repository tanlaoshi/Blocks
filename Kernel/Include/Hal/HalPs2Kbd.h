/*
 * HalPs2Kbd.h — PS/2 键盘门面（K15 最小）
 *
 * 【初学者】
 * QEMU GTK 窗按键通常进 i8042，不是 COM1。
 * X64 真读 0x60/0x64；其它 Arch stub。USB HID 枚举另刀。
 */
#ifndef HAL_PS2_KBD_H
#define HAL_PS2_KBD_H

#include "BootTypes.h"

/* 排空缓冲；成功 0（无控制器也 0，Ready=0） */
int HalPs2KbdInit(void);
int HalPs2KbdReady(void);
/*
 * 若有可打印/控制 ASCII，写入 *Out 并返回 1；
 * 无数据或忽略的扫描码返回 0。
 */
int HalPs2KbdPollChar(char *Out);

#endif
