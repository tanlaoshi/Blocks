/*
 * Usb.h — USB 模块（胶水）
 *
 * MapMmio + HalXhci（复位/端口 CCS）。枚举/HID/MSC 后刀。
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
