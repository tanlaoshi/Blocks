/*
 * UsbHostController.c — UHCI 登记进公共口。
 * 这一刀不改口扫描。传输函数还是空的，有 xHCI 或 EHCI 时不会被选中。
 */
#include "UsbHostController.h"

static const USB_HOST_CONTROLLER_OPERATIONS gOperations = { 0 };

void UhciHostControllerOffer(void) {
    Attach(USB_HOST_CONTROLLER_UHCI, &gOperations);
}
