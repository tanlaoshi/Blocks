/*
 * Private.h — 标准 Hub 请求的 SETUP 字节。
 * xHCI 与 EHCI 都用这一份。队列、门铃、调度留在各自文件。
 */
#ifndef HUB_PRIVATE_H
#define HUB_PRIVATE_H

#include "UsbHostController.h"

/* GET_STATUS，端口。wIndex 是端口号，wLength 是 4。 */
void PortStatusSetup(USB_SETUP *Setup, UINT16 Port);

/* Set=1 是 SET_FEATURE，否则是 CLEAR_FEATURE。 */
void PortFeatureSetup(USB_SETUP *Setup, UINT16 Port, UINT16 Feature, int Set);

/* GET_DESCRIPTOR，Hub 类描述符，wValue = 0x2900。 */
void DescriptorSetup(USB_SETUP *Setup, UINT16 Length);

#endif
