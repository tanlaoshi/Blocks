/*
 * HalUsbHid.h — USB HID 键鼠门面（K52 · QEMU 薄路径）
 *
 * 【初学者】
 * 叠在已附着的 xHCI（Usb / HalXhci）上：建环 → 根口枚举 → Boot/tablet 报告。
 * Gui/Console 轮询本门面；无设备时 Ready=0，调用方回落 PS/2。
 */
#ifndef HAL_USB_HID_H
#define HAL_USB_HID_H

#include "BootTypes.h"
#include "HalPs2Mouse.h"

/* 成功 0（无控制器也 0，Ready=0）；软成功不卡死 */
int HalUsbHidInitialize(void);
int HalUsbHidKeyboardReady(void);
int HalUsbHidMouseReady(void);
/* 消化事件环并重投中断 IN */
void HalUsbHidService(void);
/* 有 ASCII 写入 *Out 返回 1 */
int HalUsbHidPollChar(char *Out);
/* 有鼠标包写入 *Out 返回 1；Absolute=1 时 Dx/Dy 为 0..32767 */
int HalUsbHidPollMouse(HAL_MOUSE_PACKET *Out);

#endif
