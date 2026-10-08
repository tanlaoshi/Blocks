/*
 * Usb.h — USB（K10：探控最小）
 *
 * 本刀：认 Boot 交出的 xHCI 基址，读 CAPLENGTH/HCIVERSION。
 * 完整环/枚举/HID/MSC 后刀替换实现。
 */
#ifndef USB_H
#define USB_H

#include "BootTypes.h"

int UsbInitialize(void);
/* 1 = 已确认 xHCI 能力寄存器可读 */
int UsbXhciReady(void);
/* Boot 交出的物理基址（可为 0） */
UINT64 UsbXhciBase(void);
/* MapMmio 后的访问虚址（可与物理相同） */
UINT64 UsbXhciVirt(void);

#endif
