/*
 * UsbMscPrivate.h — MSC 门面内部分发（UsbMsc.c / Mount / Hotplug）
 *
 * 与 BulkOnly 的 Private.h（CBW/CSW 字节）分开，勿混。
 */
#ifndef USB_MSC_PRIVATE_H
#define USB_MSC_PRIVATE_H

#include "UsbMscHost.h"

/* MSC_HOST_XHCI / MSC_HOST_EHCI / 0；定义在 UsbMsc.c */
extern int gMscBe;

/* 当前已认领后端；未认领返回 0。定义在 UsbMsc.c。 */
const MSC_HOST *CurrentHost(void);

#endif
